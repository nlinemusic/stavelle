// Jubelle: copy of tools/check_build_without_qt/fontproviderstub, except that
// addSymbolFont reports success so the engine loads its music fonts metadata.
#include "fontproviderstub.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "global/serialization/json.h"

using namespace muse;
using namespace muse::draw;

namespace {
bool s_realistic = false;
constexpr double DEVICE_DPI = 1200.0; // as the real font provider (engraving units)

struct GlyphBox { double x1, y1, x2, y2; }; // staff spaces, SMuFL (y up)

std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

//! code point -> glyph box, per music font family (lower case)
const std::map<char32_t, GlyphBox>* glyphBoxes(const String& family)
{
    static std::map<std::string, std::map<char32_t, GlyphBox> > cache;
    static std::map<std::string, char32_t> names;
    const std::string fam = family.toLower().toStdString();
    auto it = cache.find(fam);
    if (it != cache.end()) {
        return it->second.empty() ? nullptr : &it->second;
    }
    std::map<char32_t, GlyphBox>& boxes = cache[fam];
    const std::string root = std::string(MU_ROOT_DIR) + "/fonts/";
    if (names.empty()) {
        const std::string gn = readFile(root + "smufl/glyphnames.json");
        const JsonObject o = JsonDocument::fromJson(ByteArray(gn.data(), gn.size())).rootObject();
        for (const std::string& k : o.keys()) {
            const std::string cp = o.value(k).toObject().value("codepoint").toStdString();
            if (cp.size() > 2) {
                names[k] = char32_t(std::stoul(cp.substr(2), nullptr, 16));
            }
        }
    }
    const std::string meta = readFile(root + fam + "/" + fam + "_metadata.json");
    if (meta.empty()) {
        return nullptr;
    }
    const JsonObject bb = JsonDocument::fromJson(ByteArray(meta.data(), meta.size())).rootObject().value("glyphBBoxes").toObject();
    for (const std::string& k : bb.keys()) {
        auto n = names.find(k);
        if (n == names.end()) {
            continue;
        }
        const JsonObject g = bb.value(k).toObject();
        const JsonArray ne = g.value("bBoxNE").toArray();
        const JsonArray sw = g.value("bBoxSW").toArray();
        boxes[n->second] = { sw.at(0).toDouble(), sw.at(1).toDouble(), ne.at(0).toDouble(), ne.at(1).toDouble() };
    }
    return boxes.empty() ? nullptr : &boxes;
}

const GlyphBox* glyph(const Font& f, char32_t c)
{
    if (!s_realistic || f.type() != Font::Type::MusicSymbol) {
        return nullptr;
    }
    const std::map<char32_t, GlyphBox>* boxes = glyphBoxes(f.family().id());
    if (!boxes) {
        return nullptr;
    }
    auto it = boxes->find(c);
    return it == boxes->end() ? nullptr : &it->second;
}

double em(const Font& f)
{
    return f.pointSizeF() * DEVICE_DPI / 72.0;
}

double textWidth(const Font& f, size_t chars)
{
    return s_realistic ? 0.5 * em(f) * double(chars) : 0.0;
}
}

void FontProviderStub::setRealisticMetrics(bool on)
{
    s_realistic = on;
}

bool FontProviderStub::realisticMetrics()
{
    return s_realistic;
}

int FontProviderStub::addSymbolFont(const String&, const io::path_t&)
{
    return 0;
}

double FontProviderStub::lineSpacing(const Font& f) const
{
    return s_realistic ? 1.2 * em(f) : 0.0;
}

double FontProviderStub::xHeight(const Font& f) const
{
    return s_realistic ? 0.5 * em(f) : 0.0;
}

double FontProviderStub::height(const Font& f) const
{
    return s_realistic ? em(f) : 0.0;
}

double FontProviderStub::capHeight(const Font& f) const
{
    return s_realistic ? 0.7 * em(f) : 0.0;
}

double FontProviderStub::ascent(const Font& f) const
{
    return s_realistic ? 0.8 * em(f) : 0.0;
}

double FontProviderStub::descent(const Font& f) const
{
    return s_realistic ? 0.2 * em(f) : 0.0;
}

bool FontProviderStub::inFont(const Font& f, char32_t c) const
{
    return glyph(f, c) != nullptr;
}

// Text
double FontProviderStub::horizontalAdvance(const Font& f, const String& string) const
{
    return textWidth(f, string.size());
}

double FontProviderStub::horizontalAdvance(const Font& f, char32_t c) const
{
    if (const GlyphBox* g = glyph(f, c)) {
        return g->x2 * em(f) / 4.0;
    }
    return textWidth(f, 1);
}

RectF FontProviderStub::boundingRect(const Font& f, const String& string) const
{
    return s_realistic ? RectF(0, -0.8 * em(f), textWidth(f, string.size()), em(f)) : RectF();
}

RectF FontProviderStub::boundingRect(const Font& f, char32_t c) const
{
    if (const GlyphBox* g = glyph(f, c)) {
        const double sp = em(f) / 4.0; // SMuFL: a staff space is a quarter of the em
        return RectF(g->x1 * sp, -g->y2 * sp, (g->x2 - g->x1) * sp, (g->y2 - g->y1) * sp);
    }
    return s_realistic ? RectF(0, -0.8 * em(f), textWidth(f, 1), em(f)) : RectF();
}

RectF FontProviderStub::tightBoundingRect(const Font& f, const String& string) const
{
    return boundingRect(f, string);
}
