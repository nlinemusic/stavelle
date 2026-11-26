/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::style). Not part of MuseScore.
 *
 * A score's style (S7): MuseScore's style values, by MuseScore's names (Sid, generated from MuseScore's definitions
 * with their defaults: sid.h, styledefaults.cpp), with the values the score sets in its <Style>, read as MuseScore
 * reads them (MStyle::read: the names older versions used, and the values a format version changes). Layout code
 * ported from MuseScore reads the same values under the same names.
 *
 * Units are MuseScore's: spatium values in staff spaces, the spatium itself in engraving units (1/1200 inch, DPI),
 * page sizes in inches, millimetre values in millimetres.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "model/id.h"
#include "sid.h"

namespace je::model {
class Snapshot;
}

namespace je::style {
constexpr double DPI = 1200.0;           // engraving units per inch (MuseScore's)
constexpr double INCH = 25.4;
constexpr double DPMM = DPI / INCH;
constexpr double SPATIUM20 = 5.0 * (DPI / 72.0);

enum class ValueKind : uint8_t {
    Number,   // booleans, integers, reals, spatium and millimetre values, enumerations written as numbers
    Text,     // strings, enumerations written as names (alignments, placements…)
    Pair,     // points (x, y), sizes and scales (w, h)
    Color,    // r, g, b, a packed into one number (0xRRGGBBAA)
};

struct StyleDef {
    const char* name;
    ValueKind kind;
    const char* type;   // MuseScore's value type (P_TYPE), e.g. "SPATIUM"
    double a, b;        // the default (a number, or a pair)
    const char* text;   // the default text
};
extern const StyleDef STYLE_DEFS[];
extern const std::pair<Sid, Sid> ALIGN_POSITIONS[];   // ends with NOSTYLE
struct SymbolSizeStyle {
    Sid symbolSize, fontSize;
    bool tempo;
};
extern const SymbolSizeStyle SYMBOL_SIZE_STYLES[];    // ends with NOSTYLE
extern const std::array<Sid, 4> HEADER_FOOTER_STYLES[];   // header, footer, copyright, page number; ends with NOSTYLE

struct PointF {
    double x = 0.0, y = 0.0;
    bool operator==(const PointF& o) const { return x == o.x && y == o.y; }
};

class Style
{
public:
    //! MuseScore's defaults
    Style();

    //! A value's id by its name in files (NOSTYLE if there is none)
    static Sid sid(std::string_view name);
    static const StyleDef& def(Sid s) { return STYLE_DEFS[int(s)]; }

    double value(Sid s) const { return m_values[size_t(s)].a; }
    bool boolean(Sid s) const { return value(s) != 0.0; }
    int integer(Sid s) const { return int(value(s)); }
    PointF point(Sid s) const { return { m_values[size_t(s)].a, m_values[size_t(s)].b }; }
    const std::string& text(Sid s) const { return m_values[size_t(s)].text; }
    uint32_t color(Sid s) const { return uint32_t(value(s)); }
    //! The spatium in engraving units
    double spatium() const { return value(Sid::spatium); }

    void set(Sid s, double v) { m_values[size_t(s)].a = v; }
    void setPoint(Sid s, PointF p) { m_values[size_t(s)].a = p.x, m_values[size_t(s)].b = p.y; }
    void setText(Sid s, std::string t) { m_values[size_t(s)].text = std::move(t); }
    bool isDefault(Sid s) const;

    using Attributes = std::vector<std::pair<std::string, std::string> >;
    //! One element of a file's <Style>, as MuseScore reads it in format `version` (470 for 4.70): false if unknown
    bool read(const std::string& name, const std::string& text, const Attributes& attrs, int version);
    //! What MuseScore changes after reading a <Style> of an older format (MStyle::read's end)
    void finishReading(int version);
    //! What MuseScore changes in a pre-4.4 score's style once it is read (CompatUtils::mapHeaderFooterStyles)
    void mapHeaderFooterStyles();
    //! A whole <Style> element (or a file holding one, as MuseScore's .mss style files)
    bool readXml(const std::string& xml, int version);

    bool operator==(const Style& o) const;

private:
    struct Value {
        double a = 0.0, b = 0.0;
        std::string text;
    };
    bool readValue(Sid s, const std::string& text, const Attributes& attrs);
    void setPositionsFromAlign(const std::vector<Sid>& ignore);
    std::vector<Value> m_values;
    bool m_chordListRead = false;
    friend Style scoreStyle(const model::Snapshot&, Id);
};

//! The format version of a score as MuseScore numbers it (440 for 4.40)
int formatVersion(const model::Snapshot& snapshot);

//! The style of a score, or of a part layout (`settings`: the layout object; none for the score): MuseScore's
//! defaults for the score (4.5's where it was written with them: defaultsVersion 450), then its own values
Style scoreStyle(const model::Snapshot& snapshot, Id settings = Id());
}
