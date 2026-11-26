/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::style). Not part of MuseScore.
 *
 * Follows MuseScore's EngravingFont (src/engraving/internal/engravingfont.cpp, GPL-3.0): which code point a symbol
 * uses, the stylistic alternates, the composed glyphs, the anchors, and the fallback to Bravura.
 */
#include "font.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_BBOX_H

#include "json.h"
#include "style.h"

#ifndef JE_FONTS_DIR
#define JE_FONTS_DIR ""
#endif

namespace je::style {
namespace {
const double DEFAULT_SPATIUM = STYLE_DEFS[int(Sid::spatium)].a;

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const std::unordered_map<std::string, SymId>& symByName()
{
    static const std::unordered_map<std::string, SymId> m = [] {
        std::unordered_map<std::string, SymId> r;
        for (int i = 0; i < SYM_COUNT; ++i) {
            r.emplace(SYMBOL_CODES[i].name, SymId(i));
        }
        return r;
    }();
    return m;
}

char32_t codepoint(const std::string& u)
{
    return u.size() > 2 && (u[0] == 'U' || u[0] == 'u') ? char32_t(std::strtoul(u.c_str() + 2, nullptr, 16)) : 0;
}

//! A font file read by FreeType, unhinted and unscaled (font units)
struct Face {
    FT_Library lib = nullptr;
    FT_Face face = nullptr;
    double scale = 0.0;   // font units → engraving units at the default spatium (an em is 4 staff spaces)

    ~Face()
    {
        if (face) {
            FT_Done_Face(face);
        }
        if (lib) {
            FT_Done_FreeType(lib);
        }
    }
    bool open(const std::string& path)
    {
        if (FT_Init_FreeType(&lib) || FT_New_Face(lib, path.c_str(), 0, &face) || !face->units_per_EM) {
            return false;
        }
        scale = 4.0 * DEFAULT_SPATIUM / double(face->units_per_EM);
        return true;
    }
    //! Metrics of a character; false if the font has no glyph for it
    bool metrics(char32_t c, RectF& bbox, double& advance) const
    {
        const FT_UInt index = FT_Get_Char_Index(face, FT_ULong(c));
        if (!index || FT_Load_Glyph(face, index, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING)) {
            return false;
        }
        FT_BBox b {};
        if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE && FT_Outline_Get_BBox(&face->glyph->outline, &b) == 0
            && face->glyph->outline.n_points > 0) {
            bbox = { double(b.xMin) * scale, -double(b.yMax) * scale, double(b.xMax - b.xMin) * scale, double(b.yMax - b.yMin) * scale };
        } else {
            bbox = RectF();   // an empty glyph (a space): not valid, as in MuseScore
        }
        advance = double(face->glyph->metrics.horiAdvance) * scale;
        return true;
    }
};

std::mutex& fontsMutex()
{
    static std::mutex m;
    return m;
}
}

RectF RectF::united(const RectF& o) const
{
    if (isNull()) {
        return o;
    }
    if (o.isNull()) {
        return *this;
    }
    const double l = std::min(left(), o.left());
    const double t = std::min(top(), o.top());
    const double r = std::max(right(), o.right());
    const double b = std::max(bottom(), o.bottom());
    return { l, t, r - l, b - t };
}

const std::vector<MusicFontInfo>& musicFonts()
{
    // MuseScore's EngravingModule::onInit and its font resources
    static const std::vector<MusicFontInfo> fonts {
        { "Bravura", "Bravura", "bravura/Bravura.otf", "bravura/bravura_metadata.json" },
        { "Leland", "Leland", "leland/Leland.otf", "leland/leland_metadata.json" },
        { "Emmentaler", "MScore", "mscore/MScore.otf", "mscore/metadata.json" },
        { "Gonville", "Gootville", "gootville/Gootville.otf", "gootville/metadata.json" },
        { "MuseJazz", "MuseJazz", "musejazz/MuseJazz.otf", "musejazz/metadata.json" },
        { "Petaluma", "Petaluma", "petaluma/Petaluma.otf", "petaluma/petaluma_metadata.json" },
        { "Finale Maestro", "Finale Maestro", "finalemaestro/FinaleMaestro.otf", "finalemaestro/FinaleMaestro.json" },
        { "Sebastian", "Sebastian", "sebastian/Sebastian.otf", "sebastian/Sebastian.json" },
        { "Eugene", "Eugene", "eugene/Eugene.otf", "eugene/Eugene.json" },
        { "Leipzig", "Leipzig", "leipzig/Leipzig.ttf", "leipzig/leipzig_metadata.json" },
        { "Finale Broadway", "Finale Broadway", "finalebroadway/FinaleBroadway.otf", "finalebroadway/FinaleBroadway.json" },
    };
    return fonts;
}

std::string defaultFontsDir()
{
    return JE_FONTS_DIR;
}

std::shared_ptr<const MusicFont> MusicFont::byName(const std::string& name, const std::string& fontsDir)
{
    const std::string dir = fontsDir.empty() ? defaultFontsDir() : fontsDir;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::shared_ptr<const MusicFont> fb;
    if (lower != "bravura") {
        fb = fallback(dir);
    }
    std::lock_guard<std::mutex> lock(fontsMutex());
    static std::map<std::string, std::shared_ptr<const MusicFont> > loaded;
    const std::string key = dir + "\n" + lower;
    if (const auto it = loaded.find(key); it != loaded.end()) {
        return it->second;
    }
    for (const MusicFontInfo& info : musicFonts()) {
        std::string n = info.name;
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (n != lower) {
            continue;
        }
        std::shared_ptr<MusicFont> f(new MusicFont());
        f->m_fallback = fb;
        std::string error;
        if (!f->load(info.name, info.family, dir + "/" + info.file, dir + "/" + info.metadata, error)) {
            loaded[key] = nullptr;
            return nullptr;
        }
        loaded[key] = f;
        return f;
    }
    return nullptr;   // MuseScore uses the fallback font for an unknown name: see byName's callers
}

std::shared_ptr<const MusicFont> MusicFont::fallback(const std::string& fontsDir)
{
    return byName("Bravura", fontsDir);
}

bool MusicFont::load(const std::string& name, const std::string& family, const std::string& fontFile,
                     const std::string& metadataFile, std::string& error)
{
    m_name = name;
    m_family = family;
    m_syms.assign(SYM_COUNT, Sym());
    Face face;
    if (!face.open(fontFile)) {
        error = "the font can't be read: " + fontFile;
        return false;
    }
    // EngravingFont::computeMetrics: the SMuFL code point, else the Musical Symbols block's
    auto compute = [&](Sym& sym, char32_t smufl, char32_t block) {
        RectF box;
        double adv = 0.0;
        if (smufl && face.metrics(smufl, box, adv)) {
            sym.code = smufl;
        } else if (block && face.metrics(block, box, adv)) {
            sym.code = block;
        } else {
            return;
        }
        sym.bbox = box;
        sym.advance = adv;
    };
    for (int i = 1; i < SYM_COUNT; ++i) {
        compute(m_syms[size_t(i)], SYMBOL_CODES[i].smufl, SYMBOL_CODES[i].musicSymbolBlock);
    }
    json::Value meta;
    if (!json::parse(readFile(metadataFile), meta, error) || !meta.isObject()) {
        error = "the font's metadata can't be read: " + metadataFile + " (" + error + ")";
        return false;
    }
    // loadGlyphsWithAnchors
    if (const json::Value* anchors = meta.get("glyphsWithAnchors")) {
        static const std::pair<const char*, SmuflAnchor> anchorNames[] = {
            { "stemDownNW", SmuflAnchor::stemDownNW }, { "stemUpSE", SmuflAnchor::stemUpSE },
            { "stemDownSW", SmuflAnchor::stemDownSW }, { "stemUpNW", SmuflAnchor::stemUpNW },
            { "cutOutNE", SmuflAnchor::cutOutNE }, { "cutOutNW", SmuflAnchor::cutOutNW }, { "cutOutSE", SmuflAnchor::cutOutSE },
            { "cutOutSW", SmuflAnchor::cutOutSW }, { "opticalCenter", SmuflAnchor::opticalCenter },
        };
        for (const auto& [glyph, value] : anchors->object) {
            SymId id = SymId::noSym;
            if (const auto it = symByName().find(glyph); it != symByName().end()) {
                id = it->second;
            } else {
                for (const GlyphAlternate* a = GLYPH_ALTERNATES; a->glyph; ++a) {
                    if (glyph == a->alternate) {
                        id = a->sym;
                        break;
                    }
                }
            }
            if (id == SymId::noSym || !value.isObject()) {
                continue;
            }
            Sym& sym = m_syms[size_t(id)];
            for (const auto& [anchorName, point] : value.object) {
                for (const auto& [n, a] : anchorNames) {
                    if (anchorName == n && point.isArray() && point.array.size() >= 2) {
                        sym.hasAnchor[size_t(a)] = true;
                        sym.anchors[size_t(a)] = { point.array[0].number * DEFAULT_SPATIUM, -point.array[1].number * DEFAULT_SPATIUM };
                    }
                }
            }
        }
    }
    // loadComposedGlyphs
    for (const ComposedGlyph& c : COMPOSED_GLYPHS) {
        Sym& sym = m_syms[size_t(c.sym)];
        if (!sym.isValid()) {
            sym.subSymbols = c.parts;
            sym.bbox = bbox(c.parts, 1.0);
        }
    }
    // loadStylisticAlternates
    if (const json::Value* alternates = meta.get("glyphsWithAlternates")) {
        for (const GlyphAlternate* a = GLYPH_ALTERNATES; a->glyph; ++a) {
            const json::Value* g = alternates->get(a->glyph);
            const json::Value* list = g ? g->get("alternates") : nullptr;
            if (!list || !list->isArray()) {
                continue;
            }
            for (const json::Value& v : list->array) {
                if (v.str("name") == a->alternate) {
                    const char32_t smufl = codepoint(v.str("codepoint"));
                    const char32_t block = codepoint(v.str("alternateCodepoint"));
                    if (smufl || block) {
                        compute(m_syms[size_t(a->sym)], smufl, block);
                    }
                    break;
                }
            }
        }
    }
    return true;
}

const MusicFont* MusicFont::use(SymId id) const
{
    // EngravingFont::useFallbackFont (MScore::useFallbackFont is on)
    if (!m_syms[size_t(id)].isValid() && m_fallback) {
        return m_fallback.get();
    }
    return this;
}

bool MusicFont::isValid(SymId id) const
{
    return m_syms[size_t(id)].isValid();
}

char32_t MusicFont::code(SymId id) const
{
    const Sym& s = m_syms[size_t(id)];
    return s.isValid() ? s.code : SYMBOL_CODES[int(id)].smufl;
}

RectF MusicFont::bbox(SymId id, double mag) const
{
    const MusicFont* f = use(id);
    return f->m_syms[size_t(id)].bbox.scaled(mag, mag);
}

RectF MusicFont::bbox(const std::vector<SymId>& ids, double mag) const
{
    RectF r;
    double x = 0.0;
    for (SymId id : ids) {
        r = r.united(bbox(id, mag).translated(x, 0.0));
        x += advance(id, mag);
    }
    return r;
}

double MusicFont::advance(SymId id, double mag) const
{
    const MusicFont* f = use(id);
    return f->m_syms[size_t(id)].advance * mag;
}

void MusicFont::anchor(SymId id, SmuflAnchor a, double mag, double& x, double& y) const
{
    const MusicFont* f = use(id);
    const Sym& s = f->m_syms[size_t(id)];
    x = s.hasAnchor[size_t(a)] ? s.anchors[size_t(a)].first * mag : 0.0;
    y = s.hasAnchor[size_t(a)] ? s.anchors[size_t(a)].second * mag : 0.0;
}

bool MusicFont::hasAnchor(SymId id, SmuflAnchor a) const
{
    return use(id)->m_syms[size_t(id)].hasAnchor[size_t(a)];
}

const std::vector<SymId>& MusicFont::subSymbols(SymId id) const
{
    return m_syms[size_t(id)].subSymbols;
}
}
