/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::style). Not part of MuseScore.
 *
 * Music fonts (S7), as MuseScore's EngravingFont knows them: every music symbol (SymId) with its code point in the
 * font, its bounding box and advance, and its SMuFL anchors (from the font's metadata); the stylistic alternates
 * MuseScore uses (Leland's oversized noteheads, serif tab clefs, straight flags…) and its composed ornaments; a
 * symbol the font lacks is taken from Bravura, as MuseScore does.
 *
 * Metrics come from the font's outlines through FreeType (unhinted, in font units): the exact bounding box of the
 * outline and the design advance, which is what MuseScore's font provider measures (a Qt painter path of the glyph
 * at 1200 dpi). They are kept in engraving units at the default spatium, as MuseScore keeps them, and scaled by the
 * caller's magnification (magS: the item's mag × spatium / default spatium).
 */
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "symid.h"

namespace je::style {
struct SymbolCode {
    const char* name;            // SMuFL / MuseScore name
    char32_t smufl;              // the SMuFL code point
    char32_t musicSymbolBlock;   // the alternate one in Unicode's Musical Symbols block (tried second)
};
extern const SymbolCode SYMBOL_CODES[SYM_COUNT];
struct GlyphAlternate {
    const char* glyph;       // the glyph whose alternate it is
    const char* alternate;   // the alternate's name in the metadata
    SymId sym;               // the symbol it is used for
};
extern const GlyphAlternate GLYPH_ALTERNATES[];   // ends with a null glyph
struct ComposedGlyph {
    SymId sym;
    std::vector<SymId> parts;
};
extern const ComposedGlyph COMPOSED_GLYPHS[7];

enum class SmuflAnchor : uint8_t { stemDownNW, stemUpSE, stemDownSW, stemUpNW, cutOutNE, cutOutNW, cutOutSE, cutOutSW, opticalCenter, COUNT };

struct RectF {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;   // y down, as in MuseScore
    double left() const { return x; }
    double top() const { return y; }
    double right() const { return x + w; }
    double bottom() const { return y + h; }
    double width() const { return w; }
    double height() const { return h; }
    bool isValid() const { return w > 0.0 && h > 0.0; }
    bool isNull() const { return w == 0.0 && h == 0.0; }
    RectF scaled(double sx, double sy) const { return { x * sx, y * sy, w * sx, h * sy }; }
    RectF translated(double dx, double dy) const { return { x + dx, y + dy, w, h }; }
    RectF united(const RectF& o) const;
};

class MusicFont
{
public:
    //! A music font of MuseScore's (by its name in the style: "Leland", "Bravura", "Emmentaler"…), loaded from the
    //! repository's fonts folder (`fontsDir`, by default the one the engine was built in). Null if it isn't known or
    //! can't be read. Fonts are loaded once and shared.
    static std::shared_ptr<const MusicFont> byName(const std::string& name, const std::string& fontsDir = std::string());
    //! The font MuseScore falls back to for symbols a font lacks (Bravura)
    static std::shared_ptr<const MusicFont> fallback(const std::string& fontsDir = std::string());
    //! The font's name and family ("Emmentaler" is the family "MScore")
    const std::string& name() const { return m_name; }
    const std::string& family() const { return m_family; }

    bool isValid(SymId id) const;
    char32_t code(SymId id) const;
    //! In engraving units at the default spatium, times `mag`
    RectF bbox(SymId id, double mag = 1.0) const;
    RectF bbox(const std::vector<SymId>& ids, double mag = 1.0) const;
    double width(SymId id, double mag = 1.0) const { return bbox(id, mag).width(); }
    double height(SymId id, double mag = 1.0) const { return bbox(id, mag).height(); }
    double advance(SymId id, double mag = 1.0) const;
    //! A SMuFL anchor (0, 0 if the glyph has none)
    void anchor(SymId id, SmuflAnchor a, double mag, double& x, double& y) const;
    bool hasAnchor(SymId id, SmuflAnchor a) const;
    //! The symbols an ornament is composed of (MuseScore's composed glyphs), if the font has none of its own
    const std::vector<SymId>& subSymbols(SymId id) const;

    //! The text in the score's "musicalTextFont" style value for this font ("Leland Text")
    std::string textFont() const { return m_family + " Text"; }

    struct Sym {
        char32_t code = 0;
        RectF bbox;
        double advance = 0.0;
        std::array<bool, size_t(SmuflAnchor::COUNT)> hasAnchor {};
        std::array<std::pair<double, double>, size_t(SmuflAnchor::COUNT)> anchors {};
        std::vector<SymId> subSymbols;
        bool isValid() const { return code != 0 && bbox.isValid(); }
    };

private:
    MusicFont() = default;
    bool load(const std::string& name, const std::string& family, const std::string& fontFile, const std::string& metadata,
              std::string& error);
    const MusicFont* use(SymId id) const;   // this font, or the fallback if it lacks the symbol

    std::string m_name;
    std::string m_family;
    std::vector<Sym> m_syms;
    std::shared_ptr<const MusicFont> m_fallback;
};

//! The music fonts MuseScore installs: name, family, font file and metadata file (relative to the fonts folder)
struct MusicFontInfo {
    const char* name;
    const char* family;
    const char* file;
    const char* metadata;
};
const std::vector<MusicFontInfo>& musicFonts();
//! The repository's fonts folder (where the engine was built)
std::string defaultFontsDir();
}
