/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). See jazzlook.h.
 */
#include "jazzlook.h"

#include <cstring>

#include "../defaultstyle.h"
#include "../style.h"
#include "dom/score.h"
#include "editing/editstyle.h"

namespace mu::engraving::jubelle {
using muse::String;

static const char16_t* JAZZ_MUSIC_FONT = u"MuseJazz";
static const char16_t* JAZZ_TEXT_FONT = u"MuseJazz Text";

static bool isFontFace(Sid sid)
{
    const char* name = MStyle::valueName(sid);
    const size_t n = name ? std::strlen(name) : 0;
    return n > 8 && std::strcmp(name + n - 8, "FontFace") == 0;
}

bool hasJazzLook(const MStyle& style)
{
    return style.styleSt(Sid::musicalSymbolFont) == JAZZ_MUSIC_FONT;
}

std::unordered_map<Sid, PropertyValue> jazzLookChanges(const MStyle& current)
{
    const MStyle& defaults = DefaultStyle::defaultStyle();
    std::unordered_map<Sid, PropertyValue> out;
    auto set = [&](Sid sid, const PropertyValue& v) {
        if (current.value(sid) != v) {
            out[sid] = v;
        }
    };
    set(Sid::musicalSymbolFont, String(JAZZ_MUSIC_FONT));
    set(Sid::musicalTextFont, String(JAZZ_TEXT_FONT));
    set(Sid::chordStyle, ChordStylePreset::JAZZ);
    set(Sid::chordsXmlFile, false);
    set(Sid::chordDescriptionFile, String(u"chords_jazz.xml"));
    for (int i = 0; i < int(Sid::STYLES); ++i) {
        const Sid sid = Sid(i);
        // only text fonts still at MuseScore's default: your own choices stay
        if (isFontFace(sid) && current.value(sid) == defaults.value(sid)) {
            set(sid, String(JAZZ_TEXT_FONT));
        }
    }
    return out;
}

std::unordered_map<Sid, PropertyValue> standardLookChanges(const MStyle& current)
{
    const MStyle& defaults = DefaultStyle::defaultStyle();
    std::unordered_map<Sid, PropertyValue> out;
    auto reset = [&](Sid sid) {
        if (current.value(sid) != defaults.value(sid)) {
            out[sid] = defaults.value(sid);
        }
    };
    if (hasJazzLook(current)) {
        reset(Sid::musicalSymbolFont);
    }
    if (current.styleSt(Sid::musicalTextFont) == JAZZ_TEXT_FONT) {
        reset(Sid::musicalTextFont);
    }
    if (current.value(Sid::chordStyle) == PropertyValue(ChordStylePreset::JAZZ)) {
        reset(Sid::chordStyle);
        reset(Sid::chordsXmlFile);
        reset(Sid::chordDescriptionFile);
    }
    for (int i = 0; i < int(Sid::STYLES); ++i) {
        const Sid sid = Sid(i);
        if (isFontFace(sid) && current.styleSt(sid) == JAZZ_TEXT_FONT) {
            reset(sid);
        }
    }
    return out;
}

size_t applyJazzLook(Score* score, bool jazz)
{
    if (!score) {
        return 0;
    }
    const std::unordered_map<Sid, PropertyValue> changes = jazz ? jazzLookChanges(score->style()) : standardLookChanges(score->style());
    if (changes.empty()) {
        return 0;
    }
    // the whole style in one step, so the music font itself is switched too
    // (changing single values doesn't reload the music font)
    MStyle style = score->style();
    for (const auto& [sid, value] : changes) {
        style.set(sid, value);
    }
    score->undo(new ChangeStyle(score, style));
    return changes.size();
}
}
