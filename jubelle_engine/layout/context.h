/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * What the layout stages need to know about a score, gathered once from a snapshot (S7): its style and music
 * font, the staves in score order (MuseScore's staff index), the measures, and each staff's timelines of clefs,
 * key signatures and transpositions as the score shows them (written or concert pitch).
 */
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "model/fraction.h"
#include "model/id.h"
#include "model/model.h"
#include "style/font.h"
#include "style/style.h"

namespace je::layout {
using style::Sid;
using style::SymId;

//! A key signature as MuseScore's KeySigEvent holds it: the key shown (written or concert) and a custom key's
//! accidentals (each on a degree from the key's first)
struct KeyEvent {
    int key = 0;
    bool custom = false;
    struct CustDef {
        int degree = 0;
        SymId sym = SymId::noSym;
    };
    std::vector<CustDef> defs;
};

struct StaffInfo {
    Id id;
    Id player;
    int index = 0;
    std::string group;           // "pitched", "percussion", "tablature" (the model's SUBKIND)
    std::vector<Id> voices;      // by number (an invalid id where the staff has no such voice)
    std::vector<int> partStaves; // the indexes of its instrument's staves, in order
    bool drumset = false;        // its instrument plays a drum set (MuseScore's Instrument::useDrumset)
    bool shown = true;           // its instrument is shown (MuseScore's Part::show; a hidden one isn't laid out)
    int lines = 5;               // its staff type's lines and step offset (StaffType::middleLine: lines - 1 - stepOffset)
    int stepOffset = 0;
};

struct MeasureInfo {
    Id id;
    Fraction start, length;
    Fraction end() const { return start + length; }
};

class ScoreContext
{
public:
    explicit ScoreContext(const model::Snapshot& s);

    const model::Snapshot& snapshot() const { return m_s; }
    const style::Style& style() const { return m_style; }
    const style::MusicFont& font() const { return *m_font; }
    bool concertPitch() const { return m_concertPitch; }
    //! The file's MuseScore version (470 for 4.70), for readings that changed between versions
    int version() const { return m_version; }
    const std::vector<StaffInfo>& staves() const { return m_staves; }
    const std::vector<MeasureInfo>& measures() const { return m_measures; }
    int staffIndex(Id staff) const;
    //! Whether a staff's measure at this time is stemless (Measure::stemless: its chords show no stems or beams)
    bool stemlessMeasure(int staff, const Fraction& t) const;
    //! The staff type's middle line at a time (StaffType::middleLine, with the staff type changes)
    int middleLine(int staff, const Fraction& t) const;
    const model::Object* obj(Id id) const { return m_s.find(id); }

    //! The clef in force at t on a staff, by MuseScore's name ("G", "F8vb"…), as shown (a transposing clef unless
    //! the score is in concert pitch)
    const std::string& clefAt(int staff, const Fraction& t) const;
    //! Its ClefInfo (G when unknown)
    const style::ClefDef& clefInfo(const std::string& name) const;
    //! The key in force at t on a staff, as shown
    const KeyEvent& keyAt(int staff, const Fraction& t) const;
    //! The times of the key signatures on a staff in [from, to)
    std::vector<Fraction> keyChanges(int staff, const Fraction& from, const Fraction& to) const;
    //! The instrument's transposition (written → sounding) at t, as the staff's player has it then
    struct Transposition {
        int diatonic = 0, chromatic = 0;
    };
    Transposition transpositionAt(int staff, const Fraction& t) const;

private:
    void buildTimelines();

    const model::Snapshot& m_s;
    style::Style m_style;
    std::shared_ptr<const style::MusicFont> m_font;
    bool m_concertPitch = false;
    int m_version = 470;
    std::set<std::pair<int, Fraction> > m_stemless;   // (staff, measure start)
    std::vector<std::map<Fraction, std::pair<int, int> > > m_staffTypes;   // per staff: time → (lines, step offset)
    std::vector<StaffInfo> m_staves;
    std::map<Id, int> m_staffIndex;
    std::vector<MeasureInfo> m_measures;
    std::vector<std::map<Fraction, std::string> > m_clefs;
    std::vector<std::string> m_defaultClef;
    std::vector<std::map<Fraction, KeyEvent> > m_keys;
    std::vector<std::map<Fraction, Transposition> > m_transpositions;
    KeyEvent m_noKey;
};

// ---- MuseScore's pitch spelling (pitchspelling.cpp) ----
// (triple flats and sharps included: F♭♭♭ is -8, C is 14, B♯♯♯ is 40)
constexpr int TPC_MIN = -8, TPC_MAX = 40, TPC_DELTA_SEMITONE = 7, TPC_DELTA_ENHARMONIC = 12;
constexpr int TPC_F_B = 6, TPC_B_B = 12, TPC_C = 14, TPC_F_S = 20, TPC_B_S = 26;
inline int tpc2step(int tpc)
{
    static const int steps[7] = { 3, 0, 4, 1, 5, 2, 6 };
    return steps[(tpc - TPC_MIN) % 7];
}
inline int tpc2pitch(int tpc)
{
    static const int pitches[] = { 2, -3, 4, -1, 6, 1, 8, 3, -2, 5, 0, 7, 2, 9, 4, -1, 6, 1, 8, 3, 10, 5, 0, 7, 2, 9, 4, 11,
                                   6, 1, 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 13, 8, 3, 10, 5, 12, 7, 14 };
    return pitches[tpc - TPC_MIN];
}
inline int tpc2alter(int tpc)
{
    return (tpc - TPC_MIN) / TPC_DELTA_SEMITONE - 3;
}
inline bool tpcIsValid(int tpc)
{
    return tpc >= TPC_MIN && tpc <= TPC_MAX;
}
//! The absolute staff step of a spelled pitch (absStep)
inline int absStep(int tpc, int pitch)
{
    int line = tpc2step(tpc) + (pitch / 12) * 7;
    const int tpcPitch = tpc2pitch(tpc);
    if (tpcPitch < 0) {
        line += 7;
    } else {
        line -= (tpcPitch / 12) * 7;
    }
    return line;
}
int clampEnharmonic(int tpc, bool useDoubleSharpsFlats = true);
int transposeTpc(int tpc, int diatonic, int chromatic, bool useDoubleSharpsFlats);
enum class PreferSharpFlat { NONE, SHARPS, FLATS, AUTO };
int transposeKey(int key, int diatonic, int chromatic, PreferSharpFlat prefer);
}
