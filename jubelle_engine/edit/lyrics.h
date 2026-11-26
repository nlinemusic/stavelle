/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle engine (je::edit), based on MuseScore Studio's lyric entry (NotationInteraction::navigateToLyrics,
 * navigateToNextSyllable, addMelisma, addLyricsVerse and navigateToLyricsVerse in
 * src/notation/internal/notationinteraction.cpp; Score::addText; TextBase::endEdit; Lyrics::adjustPrevious,
 * removeInvalidSegments and the melisma checks of LyricsLayout), Copyright (C) MuseScore Limited and others,
 * GPL-3.0-only.
 *
 * Lyric entry as the interface does it: a lyric is being edited; the keys that leave it (hyphen, space, underscore,
 * Enter, up and down) commit its text and move to the next syllable, word, melisma note or verse, changing the
 * syllables' dashes and extension lines as MuseScore does, across repeats and endings too (partial dashes and
 * extension lines at a repeat). A lyric left empty is removed; one just made for typing is taken back with the step
 * that made it, as in MuseScore. Every call works in the caller's transaction.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "changeset.h"
#include "operations.h"
#include "perform/repeatlist.h"

namespace je::edit {
class LyricsEntry
{
public:
    explicit LyricsEntry(Operations& ops);

    //! Score::addText(LYRICS_ODD) on a chord or rest (a grace note's main chord): a new lyric, in the verse after
    //! the event's lyrics, and starts editing it
    bool start(Transaction& tx, Id event);
    //! Starts editing an existing lyric
    bool edit(Id lyric);
    // An entry reads the score's chords and rests once: make a new one for a new lyric-editing session (after other
    // edits)

    //! The text typed in the lyric being edited so far (MuseScore's text; plain text is written as it is)
    void setText(const std::string& text);

    //! Hyphen: the next syllable of the word (the next chord, in any voice that has a lyric of the verse there)
    bool nextSyllable(Transaction& tx);
    //! Underscore: the syllable is held over the next chord (an extension line), or ends there
    bool melisma(Transaction& tx);
    //! Space (back: Shift+Space): the next (or previous) word; moveOnly (the arrow keys at the text's end): no
    //! change to the syllables
    bool nextWord(Transaction& tx, bool back = false, bool moveOnly = false);
    //! Enter: a new verse on the same chord
    bool addVerse(Transaction& tx);
    //! Up and down: the verse above or below, on the same chord
    bool changeVerse(Transaction& tx, bool up);
    //! Leaving the lyric (Escape, or a click elsewhere)
    void finish(Transaction& tx);

    //! The lyric being edited (none when editing has stopped)
    Id current() const { return m_editing ? m_cur.lyric : Id(); }
    bool editing() const { return m_editing; }

private:
    enum class Syllabic { Single, Begin, Middle, End };
    struct Where {
        Id lyric, event, staff;
        int voice = 0, verse = 0;
        bool above = false;
        Fraction time;
    };
    struct Separator {
        bool exists = false;
        Id endEvent;
    };

    // the music as MuseScore's segments see it (built at each step; lyric entry doesn't change the events)
    void scan();
    Id element(const Fraction& t, Id staff, int voice) const;
    bool isChord(Id event) const;
    bool next1(Fraction& t) const;
    bool prev1(Fraction& t) const;
    bool nextCR(Fraction& t, Id staff, int voice, bool sameStaff) const;
    Fraction endOf(Id event) const;
    Fraction nominal(Id event) const;
    size_t measureOf(const Fraction& t) const;
    Id lastChordRest(size_t measure, Id staff, int voice) const;
    Id endingBefore(const Fraction& t, Id staff, int voice) const;
    bool adjacent(const Fraction& a, const Fraction& b) const;
    bool hasFollowingJumpItem(Id event) const;
    bool hasPrecedingJumpItem(Id event) const;

    // lyrics
    Where whereOf(Id lyric) const;
    Id lyricOf(Id event, int verse, bool above) const;
    bool isAbove(const model::Object& lyric) const;
    Syllabic syllabic(Id lyric) const;
    Fraction ticks(Id lyric) const;
    void setSyllabic(Transaction& tx, Id lyric, Syllabic s);
    void setTicks(Transaction& tx, Id lyric, const Fraction& ticks, bool asProperty = true);
    void ticksChanged(Id lyric);
    Id findCR(const Fraction& t, Id staff, int voice) const;
    Id makeLyric(Transaction& tx, Id event, int verse, Syllabic s, const Where& like, bool styleByVerse);
    Id prevLyrics(const Where& w) const;
    bool isMelisma(Id lyric) const;
    void flagInvalidSegments(Id lyric);

    // partial dashes and extension lines at a repeat (MuseScore's PartialLyricsLine)
    Id prevPartialDash(const Where& w) const;
    Id partialMelismaAt(Id event, int verse, bool above) const;
    Id makePartial(Transaction& tx, const Where& at, Id event, bool melisma, const Fraction& ticks);
    void moveEnd(Transaction& tx, Id partial, const Fraction& delta);

    // MuseScore's edit session and layout
    void endEditText(Transaction& tx);
    void startEditing(Id lyric, bool newlyAdded, size_t creationMark, Transaction& tx);
    void rollback(Transaction& tx);
    void layout(Transaction& tx);

    Operations& m_ops;
    bool m_editing = false;
    Where m_cur;
    std::string m_text;
    bool m_newlyAdded = false;
    std::vector<ChangeRecord> m_creation;   // what made the lyric being edited, taken back if it stays empty
    std::map<Id, Separator> m_separators;   // the dash or extension line each lyric had at the last layout
    std::map<Id, int> m_lyricVoice;         // lyrics typed in another voice than their chord's
    std::vector<Id> m_flagged;              // lyrics whose lines are checked at the next layout
    bool m_laidOut = false;
    bool m_scanned = false;
    std::string m_stylePlacement;           // the score's lyricsPlacement style ("0": above)

    // scan()
    std::vector<Fraction> m_times;
    std::map<std::pair<Id, int>, std::map<Fraction, Id> > m_tracks;
    std::map<Id, std::vector<int> > m_voices;   // staff → voice numbers
    std::map<Id, std::pair<Id, int> > m_trackOf;   // event → staff, voice
    std::vector<Id> m_staffOrder;
    perform::RepeatList m_repeats;
    Fraction m_end;
};
}
