/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::style). Not part of MuseScore.
 *
 * Reading follows MuseScore's MStyle::read, readProperties, readStyleValCompat and readTextStyleValCompat
 * (src/engraving/style/style.cpp, GPL-3.0), and ReadStyleHook's choice of defaults.
 */
#include "style.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

#include "io/details.h"
#include "io/xml.h"
#include "model/fields.h"
#include "model/model.h"

namespace je::style {
namespace F = model::fields;

extern const char* const LEGACY_STYLE_V450[];   // styledefaults.cpp: MuseScore's legacy-style-defaults-v450.mss

namespace {
const std::unordered_map<std::string_view, Sid>& names()
{
    static const std::unordered_map<std::string_view, Sid> m = [] {
        std::unordered_map<std::string_view, Sid> r;
        for (int i = 0; i < int(Sid::STYLES); ++i) {
            r.emplace(STYLE_DEFS[i].name, Sid(i));
        }
        return r;
    }();
    return m;
}

double toDouble(const std::string& s)
{
    return std::strtod(s.c_str(), nullptr);
}

int toInt(const std::string& s)
{
    return int(std::strtol(s.c_str(), nullptr, 10));
}

std::string attr(const Style::Attributes& a, const char* name, const char* fallback = "")
{
    for (const auto& [k, v] : a) {
        if (k == name) {
            return v;
        }
    }
    return fallback;
}

PointF readPoint(const Style::Attributes& a)
{
    return { toDouble(attr(a, "x", "0")), toDouble(attr(a, "y", "0")) };
}

double readColor(const Style::Attributes& a)
{
    const uint32_t c = uint32_t(toInt(attr(a, "r", "0")) & 0xFF) << 24 | uint32_t(toInt(attr(a, "g", "0")) & 0xFF) << 16
                       | uint32_t(toInt(attr(a, "b", "0")) & 0xFF) << 8 | uint32_t(toInt(attr(a, "a", "255")) & 0xFF);
    return double(c);
}

// FontStyle (MuseScore): Normal 0, Bold 1, Italic 2, Underline 4, Strike 8
constexpr int FONT_BOLD = 1, FONT_ITALIC = 2, FONT_UNDERLINE = 4, FONT_STRIKE = 8;
// NoteSpellingType, ChordStylePreset and MeasureNumberPlacement names (as MuseScore writes them)
}

Style::Style()
    : m_values(size_t(Sid::STYLES))
{
    for (int i = 0; i < int(Sid::STYLES); ++i) {
        m_values[size_t(i)] = { STYLE_DEFS[i].a, STYLE_DEFS[i].b, STYLE_DEFS[i].text };
    }
}

Sid Style::sid(std::string_view name)
{
    const auto it = names().find(name);
    return it == names().end() ? Sid::NOSTYLE : it->second;
}

bool Style::isDefault(Sid s) const
{
    const Value& v = m_values[size_t(s)];
    const StyleDef& d = def(s);
    return v.a == d.a && v.b == d.b && v.text == d.text;
}

bool Style::operator==(const Style& o) const
{
    for (size_t i = 0; i < m_values.size(); ++i) {
        const Value& a = m_values[i];
        const Value& b = o.m_values[i];
        if (a.a != b.a || a.b != b.b || a.text != b.text) {
            return false;
        }
    }
    return true;
}

bool Style::readValue(Sid s, const std::string& text, const Attributes& attrs)
{
    Value& v = m_values[size_t(s)];
    const StyleDef& d = def(s);
    switch (d.kind) {
    case ValueKind::Number:
        v.a = std::string_view(d.type) == "REAL" || std::string_view(d.type) == "SPATIUM"
              || std::string_view(d.type) == "MILLIMETRE" ? toDouble(text) : double(toInt(text));
        break;
    case ValueKind::Text:
        v.text = text;
        if (std::string_view(d.type) == "LINE_TYPE" && !text.empty()
            && text.find_first_not_of("0123456789") == std::string::npos) {
            // pre-4.0 files: Qt's pen styles (NoPen keeps the default, as TConv::fromXml)
            switch (toInt(text)) {
            case 1: v.text = "solid";
                break;
            case 2: case 4: case 6: v.text = "dashed";
                break;
            case 3: case 5: v.text = "dotted";
                break;
            default: v.text = "solid";
            }
        }
        break;
    case ValueKind::Pair:
        if (std::string_view(d.type) == "POINT") {
            const PointF p = readPoint(attrs);
            v.a = p.x, v.b = p.y;
        } else {
            v.a = toDouble(attr(attrs, "w", "0")), v.b = toDouble(attr(attrs, "h", "0"));
        }
        break;
    case ValueKind::Color:
        v.a = readColor(attrs);
        break;
    }
    return true;
}

bool Style::read(const std::string& tag, const std::string& text, const Attributes& attrs, int version)
{
    // MStyle::read: values renamed or changed since older versions
    auto sp = [&](Sid s) { set(s, toDouble(text)); return true; };
    auto pt = [&](Sid s) { setPoint(s, readPoint(attrs)); return true; };
    auto as = [&](Sid s) { return readValue(s, text, attrs); };
    if (tag == "ChordList") {
        m_chordListRead = true;   // the score's own chord list (read with the chord symbols)
        return true;
    }
    if (tag == "TextStyle" || tag == "page-layout") {
        return true;   // obsolete
    }
    if (tag == "ottavaHook") {
        const double y = std::abs(toDouble(text));
        set(Sid::ottavaHookAbove, y);
        set(Sid::ottavaHookBelow, -y);
        return true;
    }
    if (tag == "Spatium" || tag == "spatium") {
        set(Sid::spatium, toDouble(text) * DPMM);
        return true;
    }
    if (tag == "displayInConcertPitch") {
        set(Sid::concertPitch, toInt(text) != 0);
        return true;
    }
    static const std::unordered_map<std::string_view, Sid> renamed {
        { "lyricsDashMaxLegth", Sid::lyricsDashMaxLength },
        { "dontHidStavesInFirstSystm", Sid::dontHideStavesInFirstSystem },
        { "minSpreadSpread", Sid::minStaffSpread },
        { "maxSpreadSpread", Sid::maxStaffSpread },
        { "hairpinWidth", Sid::hairpinLineWidth },
        { "firstSystemInsNameVisibility", Sid::firstSystemInstNameVisibility },
        { "pedalListStyle", Sid::pedalLineStyle },
        { "ArpeggioNoteDistance", Sid::arpeggioNoteDistance },
        { "ArpeggioAccidentalDistance", Sid::arpeggioAccidentalDistance },
        { "ArpeggioAccidentalDistanceMin", Sid::arpeggioAccidentalDistanceMin },
        { "ArpeggioLineWidth", Sid::arpeggioLineWidth },
        { "ArpeggioHookLen", Sid::arpeggioHookLen },
        { "ArpeggioHiddenInStdIfTab", Sid::arpeggioHiddenInStdIfTab },
        { "tremoloStrokeStyle", Sid::tremoloStyle },
        { "systemFontFace", Sid::systemTextFontFace },
        { "systemFontSize", Sid::systemTextFontSize },
        { "systemFontSpatiumDependent", Sid::systemTextFontSpatiumDependent },
        { "systemFontStyle", Sid::systemTextFontStyle },
        { "systemAlign", Sid::systemTextAlign },
        { "systemOffsetType", Sid::systemTextOffsetType },
        { "systemPlacement", Sid::systemTextPlacement },
        { "systemPosAbove", Sid::systemTextPosAbove },
        { "systemPosBelow", Sid::systemTextPosBelow },
        { "systemMinDistance", Sid::systemTextMinDistance },
        { "systemFrameType", Sid::systemTextFrameType },
        { "systemFramePadding", Sid::systemTextFramePadding },
        { "systemFrameWidth", Sid::systemTextFrameWidth },
        { "systemFrameRound", Sid::systemTextFrameRound },
        { "systemFrameFgColor", Sid::systemTextFrameFgColor },
        { "systemFrameBgColor", Sid::systemTextFrameBgColor },
        { "staffFontFace", Sid::staffTextFontFace },
        { "staffFontSize", Sid::staffTextFontSize },
        { "staffFontSpatiumDependent", Sid::staffTextFontSpatiumDependent },
        { "staffFontStyle", Sid::staffTextFontStyle },
        { "staffAlign", Sid::staffTextAlign },
        { "staffOffsetType", Sid::staffTextOffsetType },
        { "staffPlacement", Sid::staffTextPlacement },
        { "staffPosAbove", Sid::staffTextPosAbove },
        { "staffPosBelow", Sid::staffTextPosBelow },
        { "staffMinDistance", Sid::staffTextMinDistance },
        { "staffFrameType", Sid::staffTextFrameType },
        { "staffFramePadding", Sid::staffTextFramePadding },
        { "staffFrameWidth", Sid::staffTextFrameWidth },
        { "staffFrameRound", Sid::staffTextFrameRound },
        { "staffFrameFgColor", Sid::staffTextFrameFgColor },
        { "staffFrameBgColor", Sid::staffTextFrameBgColor },
        { "dymanicsShowTabCommon", Sid::dynamicsShowTabCommon },
        { "tupletOufOfStaff", Sid::tupletOutOfStaff },
        { "lineEndToSystemEndDistance", Sid::lineEndToBarlineDistance },
    };
    const auto rn = renamed.find(tag);
    if (rn != renamed.end()) {
        return as(rn->second);   // (an old "…FrameRound" name too: MuseScore reads those before converting units)
    }
    if (tag == "beamDistance") {
        set(Sid::useWideBeams, toDouble(text) > 0.75);
        return true;
    }
    if (tag == "chordSymbolPosAbove") {
        return pt(Sid::chordSymbolAPosAbove);
    }
    if (tag == "chordSymbolPosBelow") {
        return pt(Sid::chordSymbolAPosBelow);
    }
    if ((tag == "articulationMinDistance" || tag == "propertyDistanceHead" || tag == "propertyDistanceStem"
         || tag == "propertyDistance" || tag == "bracketDistance") && version < 400) {
        return true;   // obsolete before 4.0 (MuseScore skips them)
    }
    if (tag == "chordlineThickness" && version < 410) {
        return true;   // the pre-4.1 value was wrong (MuseScore ignores it)
    }
    if ((tag == "pedalText" || tag == "pedalContinueText") && version < 420) {
        if (!text.empty()) {
            setText(Sid::pedalText, text);
        }
        return true;
    }
    if ((tag == "slurEndWidth" || tag == "slurMidWidth" || tag == "slurDottedWidth" || tag == "slurMinDistance")
        && version < 430) {
        const double v = toDouble(text);
        if (tag == "slurEndWidth") {
            set(Sid::tieEndWidth, v), set(Sid::slurEndWidth, v);
        } else if (tag == "slurMidWidth") {
            set(Sid::tieMidWidth, v), set(Sid::slurMidWidth, v);
        } else if (tag == "slurDottedWidth") {
            set(Sid::tieDottedWidth, v), set(Sid::slurDottedWidth, v);
        } else {
            set(Sid::tieMinDistance, v), set(Sid::slurMinDistance, v);
        }
        return true;
    }
    if (tag == "measureNumberOffset" && version < 440) {
        return pt(Sid::measureNumberPosAbove);
    }
    if (tag == "measureNumberPosAbove" && version < 440) {
        return pt(Sid::mmRestRangePosAbove);
    }
    if (tag == "ottavaTextAlign") {
        return true;   // obsolete
    }
    if (tag == "staffTextPosAbove" && version <= 410) {
        setPoint(Sid::staffTextPosAbove, { 0.0, toDouble(text) });
        return true;
    }
    if (tag == "staffTextMinDistance" && version <= 410) {
        return sp(Sid::staffTextMinDistance);
    }
    if (tag == "pedalBeginTextOffset" || tag == "letRingBeginTextOffset" || tag == "palmMuteBeginTextOffset"
        || tag == "defaultFontSpatiumDependent" || tag == "usePre_3_6_defaults") {
        return true;   // obsolete
    }
    if ((tag == "articulationAnchorDefault" || tag == "articulationAnchorLuteFingering"
         || tag == "articulationAnchorOther") && version < 410) {
        // TOP_STAFF and TOP_CHORD → TOP (0), BOTTOM_STAFF and BOTTOM_CHORD → BOTTOM (1), else AUTO (2)
        const int old = toInt(text);
        set(sid(tag), old == 0 || old == 3 ? 0 : old == 1 || old == 4 ? 1 : 2);
        return true;
    }
    if (tag == "useStandardNoteNames" || tag == "useGermanNoteNames" || tag == "useFullGermanNoteNames"
        || tag == "useSolfeggioNoteNames" || tag == "useFrenchNoteNames") {
        if (toInt(text)) {
            setText(Sid::chordSymbolSpelling, tag == "useStandardNoteNames" ? "standard"
                    : tag == "useGermanNoteNames" ? "german" : tag == "useFullGermanNoteNames" ? "germanPure"
                    : tag == "useSolfeggioNoteNames" ? "solfeggio" : "french");
        }
        return true;
    }
    if ((tag == "chordModifierAdjust" || tag == "chordExtensionAdjust") && version < 460) {
        // pre-4.6 fifths of a spatium, now a percentage of the root's cap height (Edwin at 10pt: 121 units)
        set(sid(tag), ((toDouble(text) / 5) * def(Sid::spatium).a) / 121.0);
        return true;
    }
    if (tag == "chordDescriptionFile" && version < 460) {
        setText(Sid::chordDescriptionFile, text == "chords_std.xml" ? "chords_legacy.xml" : text);
        return true;
    }
    if (tag == "chordStyle" && version < 460) {
        setText(Sid::chordStyle, text == "std" ? "legacy" : text);
        return true;
    }
    if (tag == "fretFrets" && version < 460) {
        return true;
    }
    if ((tag == "measureNumberHPlacement" || tag == "mmRestRangeHPlacement") && version < 460) {
        // PlacementH (LEFT 0, CENTER 1, RIGHT 2) written as a number, now AlignH
        const int p = toInt(text);
        const std::string h = p == 0 ? "left" : p == 1 ? "center" : "right";
        setText(sid(tag), h);
        if (tag == "measureNumberHPlacement" && h != "left") {
            set(Sid::measureNumberAlignToBarline, false);
        }
        return true;
    }
    if (tag == "measureNumberAllStaves" || tag == "measureNumberAllStaffs") {
        setText(Sid::measureNumberPlacementMode, toInt(text) ? "on-all-staves" : "above-system");
        return true;
    }
    if (version < 470 && tag.find("FrameRound") != std::string::npos) {
        const Sid s = sid(tag);
        if (s != Sid::NOSTYLE) {
            // raster units at 360 dpi, halved when drawn; now in staff spaces
            set(s, toDouble(text) * (DPI / 360.0) / def(Sid::spatium).a / 2);
        }
        return s != Sid::NOSTYLE;
    }
    // readProperties
    const Sid s = sid(tag);
    if (s != Sid::NOSTYLE) {
        return readValue(s, text, attrs);
    }
    // readStyleValCompat
    if (tag == "tempoOffset") {
        const PointF p = readPoint(attrs);
        setPoint(Sid::tempoPosAbove, p);
        setPoint(Sid::tempoPosBelow, p);
        return true;
    }
    // readTextStyleValCompat: …FontBold, …FontItalic, …FontUnderline, …FontStrike into …FontStyle
    static const std::pair<std::string_view, int> endings[] = {
        { "FontBold", FONT_BOLD }, { "FontItalic", FONT_ITALIC }, { "FontUnderline", FONT_UNDERLINE }, { "FontStrike", FONT_STRIKE },
    };
    for (const auto& [ending, bit] : endings) {
        if (tag.size() > ending.size() && tag.compare(tag.size() - ending.size(), ending.size(), ending) == 0) {
            const Sid fs = sid(tag.substr(0, tag.size() - ending.size()) + "FontStyle");
            if (fs == Sid::NOSTYLE) {
                return false;
            }
            int v = integer(fs);
            v = toInt(text) ? v | bit : v & ~bit;
            set(fs, v);
            return true;
        }
    }
    return false;
}

void Style::finishReading(int version)
{
    if (version < 470) {
        set(Sid::dividerLeftAlignToSystemBarline, false);
        set(Sid::dividerRightAlignToSystemBarline, false);
        // CompatUtils::setMusicSymbolSize470: music symbols in text have their own size since 4.70
        for (const SymbolSizeStyle* t = SYMBOL_SIZE_STYLES; t->symbolSize != Sid::NOSTYLE; ++t) {
            set(t->symbolSize, value(t->fontSize) * (t->tempo ? 5.0 / 3.0 : 1.0));
        }
        setPositionsFromAlign({ Sid::chordSymbolAAlign, Sid::chordSymbolBAlign, Sid::romanNumeralAlign,
                                Sid::nashvilleNumberAlign, Sid::repeatLeftAlign, Sid::repeatRightAlign });
        if (text(Sid::chordStyle) == "jazz") {
            set(Sid::harmonyParenUseSmuflSym, true);
        }
    }
    if (version < 460) {
        set(Sid::verticallyAlignChordSymbols, value(Sid::maxChordShiftAbove) != 0.0 || value(Sid::maxChordShiftBelow) != 0.0
            || value(Sid::maxFretShiftAbove) != 0.0 || value(Sid::maxFretShiftBelow) != 0.0);
        setPositionsFromAlign({});
        if (text(Sid::measureNumberPosition) == "center") {
            setText(Sid::measureNumberHPlacement, "center");
        }
        if (integer(Sid::pedalPlacement) == 1 /* BELOW */ && value(Sid::pedalHookHeight) < 0) {
            set(Sid::pedalHookHeight, -value(Sid::pedalHookHeight));
        }
        if (value(Sid::ottavaHookBelow) < 0) {
            set(Sid::ottavaHookBelow, -value(Sid::ottavaHookBelow));
        }
        set(Sid::repeatPlayCountShow, false);
        set(Sid::harmonyHarmonyDistance, value(Sid::minHarmonyDistance));
    }
    if (version == 450) {
        set(Sid::spacingDensity, 1.30);   // 4.50's spacing was a little narrower
    }
    if (version < 450) {
        for (Sid s : { Sid::scaleRythmicSpacingForSmallNotes, Sid::maskBarlinesForText, Sid::showCourtesiesRepeats,
                       Sid::showCourtesiesOtherJumps, Sid::showCourtesiesAfterCancellingRepeats,
                       Sid::showCourtesiesAfterCancellingOtherJumps, Sid::changesBeforeBarlineRepeats,
                       Sid::changesBeforeBarlineOtherJumps }) {
            set(s, false);
        }
    }
    if (version < 420) {
        setText(Sid::tiePlacementChord, "inside");   // (not in MuseScore's test mode)
    }
}

void Style::setPositionsFromAlign(const std::vector<Sid>& ignore)
{
    // CompatUtils::setPositionStylesFromAlign: a position value takes its alignment's horizontal part
    for (const std::pair<Sid, Sid>* p = ALIGN_POSITIONS; p->first != Sid::NOSTYLE; ++p) {
        if (std::find(ignore.begin(), ignore.end(), p->first) != ignore.end()) {
            continue;
        }
        const std::string& align = text(p->first);
        setText(p->second, align.substr(0, align.find(',')));
    }
}

void Style::mapHeaderFooterStyles()
{
    // CompatUtils::mapHeaderFooterStyles (pre-4.4 scores): copyright and page numbers were styled as the header or
    // footer whose text holds their macro ($c, $C; $p, $P, $n, $N)
    auto has = [&](Sid sid, const char* letters) {
        const std::string& t = text(sid);
        for (size_t i = 0; i + 1 < t.size(); ++i) {
            if (t[i] == '$' && std::string_view(letters).find(t[i + 1]) != std::string_view::npos) {
                return true;
            }
        }
        return false;
    };
    auto map = [&](int from, const std::vector<Sid>& strings) {
        bool copyright = false, pageNumber = false;
        for (Sid sid : strings) {
            if (!copyright && has(sid, "cC")) {
                for (const auto* row = HEADER_FOOTER_STYLES; row->at(0) != Sid::NOSTYLE; ++row) {
                    m_values[size_t(row->at(2))] = m_values[size_t(row->at(size_t(from)))];
                }
                copyright = true;
            }
            if (!pageNumber && has(sid, "pPnN")) {
                for (const auto* row = HEADER_FOOTER_STYLES; row->at(0) != Sid::NOSTYLE; ++row) {
                    m_values[size_t(row->at(3))] = m_values[size_t(row->at(size_t(from)))];
                }
                pageNumber = true;
            }
        }
    };
    map(0, { Sid::oddHeaderL, Sid::oddHeaderC, Sid::oddHeaderR, Sid::evenHeaderL, Sid::evenHeaderC, Sid::evenHeaderR });
    map(1, { Sid::oddFooterL, Sid::oddFooterC, Sid::oddFooterR, Sid::evenFooterL, Sid::evenFooterC, Sid::evenFooterR });
}

bool Style::readXml(const std::string& text, int version)
{
    io::xml::Document doc;
    std::string error;
    if (!io::xml::parse(text, doc, error)) {
        return false;
    }
    const io::xml::Node* style = doc.root.name == "Style" ? &doc.root : doc.root.child("Style");
    if (!style) {
        return false;
    }
    for (const io::xml::Node& c : style->children) {
        if (c.type != io::xml::Node::Type::Element) {
            continue;
        }
        Attributes attrs;
        for (const auto& [k, v] : c.attrs) {
            attrs.emplace_back(k, io::xml::decode(v));
        }
        read(c.name, c.text(), attrs, version);
    }
    finishReading(version);
    return true;
}

int formatVersion(const model::Snapshot& s)
{
    const model::Object* score = s.find(s.score());
    const std::string v = score ? score->text(F::SOURCE_FORMAT) : std::string();
    if (v.empty()) {
        return 470;
    }
    const size_t dot = v.find('.');
    return dot == std::string::npos ? toInt(v) * 100 : toInt(v.substr(0, dot)) * 100 + toInt(v.substr(dot + 1));
}

Style scoreStyle(const model::Snapshot& s, Id settings)
{
    const model::Object* score = s.find(s.score());
    const model::Object* own = settings.isValid() ? s.find(settings) : score;
    Style st;
    if (!own) {
        return st;
    }
    // MuseScore reads score_style.mss by its own format version
    int version = formatVersion(s);
    if (score && score->has(F::STYLE_FORMAT)) {
        const std::string v = score->text(F::STYLE_FORMAT);
        const size_t dot = v.find('.');
        version = dot == std::string::npos ? toInt(v) * 100 : toInt(v.substr(0, dot)) * 100 + toInt(v.substr(dot + 1));
    }
    const auto pairs = io::unpackPairs(own->text(F::STYLE));
    // ReadStyleHook::setupDefaultStyle: a part starts from the defaults the score was written with (4.5's for
    // defaultsVersion 450); the score itself from MuseScore's current ones (a 4.x file holds its whole style)
    std::string defaults;
    for (const auto& [name, raw] : io::unpackPairs(score ? score->text(F::STYLE) : std::string())) {
        if (name == "defaultsVersion") {
            defaults = raw;
        }
    }
    if (own != score && toInt(defaults) == 450) {
        static const std::string legacy = [] {
            std::string x;
            for (const char* const* l = LEGACY_STYLE_V450; *l; ++l) {
                x += *l;
                x += '\n';
            }
            return x;
        }();
        st.readXml(legacy, 450);
    }
    const std::string chordFileBefore = st.text(Sid::chordDescriptionFile);
    for (const auto& [name, raw] : pairs) {
        st.read(name, raw == "\x01" ? std::string() : io::xml::decode(raw), {}, version);
    }
    for (const io::xml::Node& c : io::parseFragment(own->text(F::DETAILS_2))) {
        if (c.type != io::xml::Node::Type::Element) {
            continue;
        }
        Style::Attributes attrs;
        for (const auto& [k, v] : c.attrs) {
            attrs.emplace_back(k, io::xml::decode(v));
        }
        st.read(c.name, c.text(), attrs, version);
    }
    if (own->has(F::STYLE) || own->has(F::DETAILS_2)) {   // MuseScore adjusts a style only when it reads one
        st.finishReading(version);
        // ReadChordListHook::validate: a chord description file of its own (not one of MuseScore's) makes it custom
        const std::string& file = st.text(Sid::chordDescriptionFile);
        if (!st.m_chordListRead && file != chordFileBefore && file.rfind("chords_", 0) != 0 && st.text(Sid::chordStyle) == "std") {
            st.setText(Sid::chordStyle, "custom");
            st.set(Sid::chordsXmlFile, true);
        }
    }
    if (own == score && formatVersion(s) < 440) {
        st.mapHeaderFooterStyles();
    }
    return st;
}
}
