/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * The reference for the Jubelle engine's file reader (S2, jubelle_engine/docs/15_file_handling.md): MuseScore's
 * own engine reads each listed score and writes its music in exactly the format of `je_io_tool listing`
 * (staff, voice, time, duration, "g" for grace chords, sorted pitches), so the two readings can be compared
 * file by file. MuseScore's engine is used here as the comparison baseline only, never by the Jubelle engine.
 *
 *   JUBELLE_REFERENCE_ROOT=<repository> JUBELLE_REFERENCE_LIST=<file of relative paths>
 *   JUBELLE_REFERENCE_OUT=<folder> jubelle_engine_tests --gtest_filter=Jubelle_Reference.*
 *
 * JUBELLE_REFERENCE_TIMESIG="<measure index> <num> <den>": before listing, MuseScore's own command adds that time
 * signature (Score::cmdAddTimeSig, as dropping it from the palette), for comparing the Jubelle engine's
 * changeTimeSignature (`je_io_tool tsedit`); the listing starts with "# timesig changed" or "# timesig refused".
 * JUBELLE_REFERENCE_SPLIT="<measure index>": SplitJoinMeasure::splitMeasure at the middle of that measure (where a
 * chord or rest starts there); JUBELLE_REFERENCE_JOIN="<first> <last>": joinMeasures; compared with
 * `je_io_tool measedit` ("# measures changed|refused").
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "engraving/compat/mscxcompat.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/chordlist.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/realizedharmony.h"
#include "engraving/dom/masterscore.h"
#include "engraving/style/defaultstyle.h"
#include "engraving/style/style.h"
#include "engraving/style/textstyle.h"
#include "engraving/types/typesconv.h"
#include "engraving/rw/compat/compatutils.h"
#include "engraving/rw/xmlwriter.h"
#include "engraving/infrastructure/rtti.h"
#include "global/io/buffer.h"
#include "engraving/dom/note.h"
#include "engraving/dom/accidental.h"
#include "engraving/dom/notedot.h"
#include "engraving/dom/stem.h"
#include "engraving/dom/hook.h"
#include "engraving/dom/beam.h"
#include "engraving/dom/system.h"
#include "engraving/types/symnames.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/compat/midi/compatmidirender.h"
#include "engraving/compat/midi/event.h"
#include "engraving/dom/articulation.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/linkedobjects.h"
#include "engraving/infrastructure/localfileinfoprovider.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/navigate.h"
#include "engraving/dom/utils.h"
#include "engraving/dom/textbase.h"
#include <set>
#include "engraving/dom/fingering.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/dynamic.h"
#include "engraving/types/typesconv.h"
#include "engraving/tests/utils/scorerw.h"
#include "engraving/dom/timesig.h"
#include "engraving/editing/splitjoinmeasure.h"

using namespace mu::engraving;

namespace {
std::string frac(Fraction f)
{
    f.reduce();
    return std::to_string(f.numerator()) + "/" + std::to_string(f.denominator());
}

void line(std::ostringstream& out, size_t staff, int voice, ChordRest* cr, bool grace)
{
    const Fraction dur = grace ? cr->durationTypeTicks() : cr->actualTicks();
    out << staff << ' ' << voice << ' ' << frac(cr->tick()) << ' ' << frac(dur) << (grace ? " g" : "");
    if (cr->isChord()) {
        std::vector<int> pitches;
        for (Note* n : toChord(cr)->notes()) {
            pitches.push_back(n->pitch());
        }
        std::sort(pitches.begin(), pitches.end());
        for (int p : pitches) {
            out << ' ' << p;
        }
        // JUBELLE_REFERENCE_SLASH=1: slashes marked (Chord::slash), as the engine's listing marks them
        if (std::getenv("JUBELLE_REFERENCE_SLASH") && !toChord(cr)->notes().empty() && toChord(cr)->slash()) {
            out << " /";
        }
        // JUBELLE_REFERENCE_DETAIL=1: what playback depends on too (for comparing two files read by MuseScore)
        if (std::getenv("JUBELLE_REFERENCE_DETAIL")) {
            Chord* c = toChord(cr);
            out << " |";
            for (Note* n : c->notes()) {
                out << ' ' << n->pitch() << '/' << n->tpc1() << '/' << n->tpc2() << (n->tieFor() ? "~" : "") << (n->tieBack() ? "<" : "")
                    << (n->play() ? "" : "m") << "v" << n->userVelocity();
                for (Spanner* sp : n->spannerFor()) {
                    out << " " << sp->typeName();
                }
            }
            for (Articulation* a : c->articulations()) {
                out << " a:" << int(a->symId());
            }
            out << " nt:" << int(c->noteType());
        }
    }
    if (std::getenv("JUBELLE_REFERENCE_SLASH") && cr->isRest() && toRest(cr)->accent()) {
        out << " acc";   // Rest::accent
    }
    if ((std::getenv("JUBELLE_REFERENCE_SLASH") || std::getenv("JUBELLE_REFERENCE_DETAIL")) && cr->staffMove() != 0) {
        out << " sm:" << cr->staffMove();   // cross-staff
    }
    if ((std::getenv("JUBELLE_REFERENCE_LYRICS") || std::getenv("JUBELLE_REFERENCE_TEXTS")) && !grace) {
        // lyrics: verse (a: above), syllabic, extension, text (as the engine's listing writes them)
        std::vector<std::string> ls;
        for (Lyrics* l : cr->lyrics()) {
            static const char* syl[] = { "s", "b", "e", "m" };   // LyricsSyllabic: SINGLE, BEGIN, END, MIDDLE
            ls.push_back(" L" + std::to_string(l->verse()) + (l->placeAbove() ? "a" : "") + ":" + syl[int(l->syllabic())] + ":"
                         + frac(l->ticks()) + ":" + l->xmlText().toStdString());
        }
        std::sort(ls.begin(), ls.end());
        for (const std::string& x : ls) {
            out << x;
        }
    }
    out << '\n';
}

std::string listing(MasterScore* s)
{
    std::ostringstream out;
    size_t staffIndex = 0;
    for (Part* part : s->parts()) {
        for (Staff* staff : part->staves()) {
            const size_t idx = staff->idx();
            for (int v = 0; v < 4; ++v) {
                const track_idx_t track = idx * VOICES + size_t(v);
                for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                    EngravingItem* e = seg->element(track);
                    if (!e || !e->isChordRest()) {
                        continue;
                    }
                    ChordRest* cr = toChordRest(e);
                    if (cr->isRest() && toRest(cr)->isGap()) {
                        // a gap rest: MuseScore's invisible placeholder for a hole in a voice, not music
                        // (JUBELLE_REFERENCE_SHOW_GAPS=1 lists them, marked, for investigating differences)
                        if (std::getenv("JUBELLE_REFERENCE_SHOW_GAPS")) {
                            out << "# gap " << staffIndex << ' ' << v << ' ' << frac(cr->tick()) << ' ' << frac(cr->actualTicks()) << '\n';
                        }
                        continue;
                    }
                    if (cr->isChord()) {
                        for (Chord* g : toChord(cr)->graceNotesBefore()) {
                            line(out, staffIndex, v, g, true);
                        }
                    }
                    line(out, staffIndex, v, cr, false);
                    if (cr->isChord()) {
                        for (Chord* g : toChord(cr)->graceNotesAfter()) {
                            line(out, staffIndex, v, g, true);
                        }
                    }
                }
            }
            ++staffIndex;
        }
    }
    if (std::getenv("JUBELLE_REFERENCE_ADDTEXT") || std::getenv("JUBELLE_REFERENCE_TEXTS")) {
        // texts, dynamics, tempos, rehearsal marks, chord symbols…: type, time, staff, voice, text
        std::vector<std::string> texts;
        // the kinds the engine models (as flow items); system-wide ones on the top staff only
        static const std::set<std::string> kinds { "StaffText", "SystemText", "Dynamic", "Expression", "RehearsalMark", "Tempo",
                                                   "PlayTechAnnotation", "InstrumentChange" };
        for (Segment* seg = s->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
            for (EngravingItem* a : seg->annotations()) {
                if (a->isTextBase() && kinds.count(a->typeName()) && (!a->systemFlag() || a->isTopSystemObject())) {
                    // a dynamic by its type (its text is the type's symbols unless the file says otherwise)
                    std::string t = a->isDynamic() ? std::string(TConv::toXml(toDynamic(a)->dynamicType()).ascii())
                                                   : toTextBase(a)->xmlText().toStdString();
                    for (size_t at = t.find('\n'); at != std::string::npos; at = t.find('\n', at)) {
                        t.replace(at, 1, "<br/>");   // a line break, as files write it
                    }
                    if (a->isTempoText()) {
                        char b[32];
                        std::snprintf(b, sizeof(b), " @%.4g", toTempoText(a)->tempo().val);
                        t += b;
                    }
                    texts.push_back(std::string("# text ") + a->typeName() + " " + frac(seg->tick()) + " " + std::to_string(a->staffIdx()) + " "
                                    + std::to_string(a->voice()) + " " + t);
                }
            }
        }
        // fingerings: on their notes
        for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
            for (EngravingItem* e : seg->elist()) {
                if (!e || !e->isChord()) {
                    continue;
                }
                for (Note* n : toChord(e)->notes()) {
                    for (EngravingItem* x : n->el()) {
                        if (x->isFingering()) {
                            texts.push_back("# fingering " + frac(seg->tick()) + " " + std::to_string(e->staffIdx()) + " "
                                            + std::to_string(e->voice()) + " " + std::to_string(n->pitch()) + " "
                                            + TConv::toXml(toTextBase(x)->textStyleType()).ascii() + " " + toTextBase(x)->xmlText().toStdString());
                        }
                    }
                }
            }
        }
        std::sort(texts.begin(), texts.end());
        for (const std::string& l : texts) {
            out << l << '\n';
        }
    }
    if (std::getenv("JUBELLE_REFERENCE_LYRICS") || std::getenv("JUBELLE_REFERENCE_TEXTS")) {
        // partial dashes and extension lines at repeats
        std::vector<std::string> lines;
        for (const auto& [tick, sp] : s->spanner()) {
            if (!sp->isPartialLyricsLine()) {
                continue;
            }
            const PartialLyricsLine* p = toPartialLyricsLine(sp);
            lines.push_back("# partial " + std::to_string(p->staffIdx()) + " " + std::to_string(p->voice()) + " " + frac(p->tick()) + " "
                            + frac(p->ticks()) + " " + std::to_string(p->verse()) + " " + (p->isEndMelisma() ? "m" : "d") + " "
                            + (p->placeAbove() ? "a" : "b"));
        }
        std::sort(lines.begin(), lines.end());
        for (const std::string& l : lines) {
            out << l << '\n';
        }
    }
    // JUBELLE_REFERENCE_LINKS=1: for linked staves of the score (a guitar's standard and tablature staves), how
    // many of their chords, rests and notes MuseScore linked to an element of the other staff at the same time
    if (std::getenv("JUBELLE_REFERENCE_LINKS")) {
        size_t si = 0;
        for (Part* part : s->parts()) {
            for (Staff* staff : part->staves()) {
                if (staff->links() && staff->links()->size() > 1) {
                    int elements = 0, linked = 0, wrong = 0;
                    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                        for (int v = 0; v < 4; ++v) {
                            EngravingItem* e = seg->element(staff->idx() * VOICES + size_t(v));
                            if (!e || !e->isChordRest() || (e->isRest() && toRest(e)->isGap())) {
                                continue;
                            }
                            std::vector<EngravingItem*> items { e };
                            if (e->isChord()) {
                                for (Note* n : toChord(e)->notes()) {
                                    items.push_back(n);
                                }
                            }
                            for (EngravingItem* it : items) {
                                ++elements;
                                bool ok = false;
                                if (it->links()) {
                                    for (EngravingObject* o : *it->links()) {
                                        if (o == it || !o->isEngravingItem()) {
                                            continue;
                                        }
                                        EngravingItem* other = toEngravingItem(o);
                                        if (other->score() != s) {
                                            continue;   // a part's copy
                                        }
                                        ok = true;
                                        const bool same = other->tick() == it->tick() && other->type() == it->type()
                                                          && (!it->isNote() || toNote(other)->pitch() == toNote(it)->pitch());
                                        wrong += same ? 0 : 1;
                                    }
                                }
                                linked += ok ? 1 : 0;
                                if (!ok && std::string(std::getenv("JUBELLE_REFERENCE_LINKS")) == "2") {
                                    out << "# unlinked " << it->typeName() << " staff " << si << " at " << frac(it->tick()) << "\n";
                                }
                            }
                        }
                    }
                    // and the lines and staff items on it (dynamics, staff texts, chord symbols, slurs, hairpins…)
                    auto linkedInScore = [s](EngravingItem* it) {
                        if (it->links()) {
                            for (EngravingObject* o : *it->links()) {
                                if (o != it && o->isEngravingItem() && toEngravingItem(o)->score() == s) {
                                    return true;
                                }
                            }
                        }
                        return false;
                    };
                    // (not those MuseScore keeps to their own staff of a part: Score::undoAddElement leaves them off the
                    // part's other staves, so a new one is linked to nothing)
                    static const std::set<std::string> ownStaff { "StaffText", "Dynamic", "Expression", "Harmony", "PlayTechAnnotation",
                                                                  "FretDiagram", "Capo", "StringTunings", "TripletFeel", "FiguredBass",
                                                                  "PlayCountText" };
                    for (Segment* seg = s->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
                        for (EngravingItem* a : seg->annotations()) {
                            if (a->staffIdx() == staff->idx() && !a->systemFlag() && !ownStaff.count(a->typeName())) {
                                ++elements;
                                linked += linkedInScore(a) ? 1 : 0;
                                if (!linkedInScore(a) && std::string(std::getenv("JUBELLE_REFERENCE_LINKS")) == "2") {
                                    out << "# unlinked " << a->typeName() << " staff " << si << " at " << frac(seg->tick()) << "\n";
                                }
                            }
                        }
                    }
                    for (const auto& [tick, sp] : s->spanner()) {
                        if (sp->staffIdx() == staff->idx() && !sp->systemFlag() && !sp->isTie()) {
                            ++elements;
                            linked += linkedInScore(sp) ? 1 : 0;
                            if (!linkedInScore(sp) && std::string(std::getenv("JUBELLE_REFERENCE_LINKS")) == "2") {
                                out << "# unlinked " << sp->typeName() << " staff " << si << " at " << frac(sp->tick()) << "\n";
                            }
                        }
                    }
                    const Staff* first = toStaff(staff->links()->mainElement());
                    out << "# links staff " << si << ": " << elements << " elements, " << linked << " linked, " << wrong << " wrong, with staff "
                        << (first ? first->idx() : staff->idx()) << "\n";
                }
                ++si;
            }
        }
    }
    if (std::getenv("JUBELLE_REFERENCE_MIDI")) {
        // MuseScore's MIDI rendering (what its MIDI export writes), repeats expanded
        EventsHolder events;
        CompatMidiRendererInternal::Context ctx;
        CompatMidiRender::renderScore(s, events, ctx, true);
        for (size_t ch = 0; ch < events.size(); ++ch) {
            for (const auto& [tick, ev] : events[ch]) {
                out << "# midi " << ch << ' ' << tick << ' ' << int(ev.type()) << ' ' << int(ev.dataA()) << ' ' << int(ev.dataB()) << '\n';
            }
        }
    }
    // JUBELLE_REFERENCE_HARMONY=1: every chord symbol as MuseScore understands it (quality, extension, modifiers) and
    // realizes it in each voicing, literal and jazz (RealizedHarmony::generateNotes, in concert pitch)
    if (std::getenv("JUBELLE_REFERENCE_HARMONY")) {
        for (Segment* seg = s->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
            for (EngravingItem* a : seg->annotations()) {
                if (!a->isHarmony() || toHarmony(a)->chords().size() != 1 || !toHarmony(a)->isRealizable() || !toHarmony(a)->parsedForm()) {
                    continue;
                }
                Harmony* h = toHarmony(a);
                const ParsedChord* p = h->parsedForm();
                out << "# harmony " << frac(seg->tick()) << " " << a->track() << " " << h->rootTpc() << "/" << h->bassTpc() << " "
                    << h->chords().front()->textName().toStdString() << " | " << p->quality().toStdString() << " "
                    << p->extension().toStdString() << " [";
                for (const String& m : p->modifierList()) {
                    out << " " << m.toStdString();
                }
                out << " ]" << (p->understandable() ? "" : " ?");
                if (std::getenv("JUBELLE_REFERENCE_DEBUG")) {
                    const Harmony* next = h->findNext();
                    out << " next=" << (next ? frac(next->tick()) + ":" + std::to_string(next->rootTpc()) + ":" + std::to_string(next->track()) : "none");
                }
                for (int literal = 1; literal >= 0; --literal) {
                    for (int v = 0; v <= 6; ++v) {
                        out << (v ? " " : " |");
                        const auto notes = h->realizedHarmony().generateNotes(h->rootTpc(), h->bassTpc(), literal, Voicing(v), 0);
                        for (const auto& [pitch, tpc] : notes) {
                            out << pitch << ":" << tpc << ",";
                        }
                    }
                }
                out << "\n";
            }
        }
    }
    if (std::getenv("JUBELLE_REFERENCE_DETAIL")) {
        std::vector<std::string> spanners;
        for (const auto& [tick, sp] : s->spanner()) {
            spanners.push_back(std::string("# spanner ") + sp->typeName() + " " + frac(sp->tick()) + " " + frac(sp->tick2()) + " "
                               + std::to_string(sp->track()) + "-" + std::to_string(sp->track2()) + "\n");
        }
        std::sort(spanners.begin(), spanners.end());
        for (const std::string& l : spanners) {
            out << l;
        }
        for (Segment* seg = s->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
            for (EngravingItem* a : seg->annotations()) {
                out << "# annotation " << a->typeName() << " " << frac(seg->tick()) << " " << a->track();
                if (a->isHarmony() && !toHarmony(a)->chords().empty()) {
                    // a chord symbol's root, bass and name, as read (a polychord: its first chord)
                    const Harmony* h = toHarmony(a);
                    out << " " << h->rootTpc() << "/" << h->bassTpc() << " " << h->chords().front()->textName().toStdString();
                }
                out << "\n";
            }
        }
    }
    return out.str();
}

//! MuseScore's lyric entry (NotationInteraction::navigateToLyrics, navigateToNextSyllable, addMelisma,
//! addLyricsVerse, navigateToLyricsVerse, copied with the interface left out) with TextBase::endEdit's outcome for a
//! lyric: the typed text committed with Lyrics::adjustPrevious, or the lyric removed (a new one: the command that
//! made it rolled back). Keys as je_io_tool's lyricsScript.
class LyricSession
{
public:
    explicit LyricSession(MasterScore* s)
        : m_s(s) {}
    Lyrics* cur = nullptr;
    bool newlyAdded = false;
    std::string typed;

    void startEditText(Lyrics* l, bool isNew)
    {
        cur = l;
        newlyAdded = isNew;
        typed.clear();
    }

    void endEditText()
    {
        if (!cur) {
            return;
        }
        Lyrics* l = cur;
        cur = nullptr;
        const String text = typed.empty() ? l->xmlText() : String::fromStdString(typed);
        typed.clear();
        if (!text.isEmpty()) {
            m_s->startCmd(TranslatableString::untranslatable("lyrics text"));
            if (text != l->xmlText()) {
                l->undoChangeProperty(Pid::TEXT, text);
            }
            l->adjustPrevious();
            m_s->endCmd();
            return;
        }
        if (newlyAdded) {
            m_s->undoRedo(true, nullptr);   // the "add element" command rolled back
        } else {
            m_s->startCmd(TranslatableString::untranslatable("lyrics removed"));
            PartialLyricsLine* partialDash = findPrevPartialLyricsLineDash(l);
            m_s->undoRemoveElement(l);
            if (partialDash) {
                m_s->removeElement(partialDash);
            }
            m_s->endCmd();
        }
        Lyrics* prev = prevLyrics(l);
        if (prev) {
            m_s->startCmd(TranslatableString::untranslatable("lyrics layout"));
            prev->setNeedRemoveInvalidSegments();
            prev->triggerLayout();
            m_s->setLayoutAll();
            m_s->endCmd();
        }
    }

    void navigateToLyrics(bool back, bool moveOnly)
    {
        Lyrics* lyrics = cur;
        track_idx_t track = lyrics->track();
        Segment* segment = lyrics->segment();
        int verse = lyrics->verse();
        PlacementV placement = lyrics->placement();
        PropertyFlags pFlags = lyrics->propertyFlags(Pid::PLACEMENT);
        FontStyle fStyle = lyrics->fontStyle();
        PropertyFlags fFlags = lyrics->propertyFlags(Pid::FONT_STYLE);
        TextStyleType styleType = lyrics->textStyleType();

        Segment* nextSegment = segment;
        while ((nextSegment = back ? nextSegment->prev1(SegmentType::ChordRest) : nextSegment->next1(SegmentType::ChordRest))) {
            EngravingItem* el = nextSegment->element(track);
            if (!el) {
                continue;
            }
            if (el->isChord()) {
                break;
            } else if (el->isRest()) {
                if (toChordRest(el)->lyrics(verse, placement)) {
                    break;
                }
            }
        }
        if (!nextSegment) {
            return;
        }
        endEditText();
        Lyrics* fromLyrics = nullptr;
        if (!back) {
            while (segment) {
                ChordRest* cr = toChordRest(segment->element(track));
                if (cr) {
                    fromLyrics = cr->lyrics(verse, placement);
                    if (fromLyrics) {
                        break;
                    }
                }
                segment = segment->prev1(SegmentType::ChordRest);
            }
        }
        ChordRest* cr = toChordRest(nextSegment->element(track));
        if (!cr) {
            return;
        }
        Lyrics* nextLyrics = cr->lyrics(verse, placement);
        bool newLyrics = false;
        if (!nextLyrics) {
            nextLyrics = Factory::createLyrics(cr);
            nextLyrics->setTrack(track);
            nextLyrics->setParent(cr);
            nextLyrics->setVerse(verse);
            nextLyrics->setTextStyleType(styleType);
            nextLyrics->setPlacement(placement);
            nextLyrics->setPropertyFlags(Pid::PLACEMENT, pFlags);
            nextLyrics->setSyllabic(LyricsSyllabic::SINGLE);
            nextLyrics->setFontStyle(fStyle);
            nextLyrics->setPropertyFlags(Pid::FONT_STYLE, fFlags);
            newLyrics = true;
        }
        m_s->startCmd(TranslatableString::untranslatable("Navigate to lyrics"));
        if (fromLyrics && !moveOnly) {
            switch (nextLyrics->syllabic()) {
            case LyricsSyllabic::SINGLE:
            case LyricsSyllabic::BEGIN:
                break;
            case LyricsSyllabic::END:
                nextLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::SINGLE));
                break;
            case LyricsSyllabic::MIDDLE:
                nextLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::BEGIN));
                break;
            }
            switch (fromLyrics->syllabic()) {
            case LyricsSyllabic::SINGLE:
            case LyricsSyllabic::END:
                break;
            case LyricsSyllabic::BEGIN:
                fromLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::SINGLE));
                break;
            case LyricsSyllabic::MIDDLE:
                fromLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::END));
                break;
            }
            if (fromLyrics->separator() && !fromLyrics->separator()->isEndMelisma()) {
                fromLyrics->undoChangeProperty(Pid::LYRIC_TICKS, Fraction::fromTicks(0));
            }
        }
        if (newLyrics) {
            m_s->undoAddElement(nextLyrics);
        }
        m_s->setLayoutAll();
        m_s->endCmd();
        startEditText(nextLyrics, newLyrics);
    }

    void navigateToNextSyllable()
    {
        Lyrics* lyrics = cur;
        ChordRest* initialCR = lyrics->chordRest();
        const bool hasPrecedingRepeat = initialCR->hasPrecedingJumpItem();
        const bool hasFollowingRepeat = initialCR->hasFollowingJumpItem();
        track_idx_t track = lyrics->track();
        track_idx_t toLyricTrack = track;
        Segment* segment = lyrics->segment();
        int verse = lyrics->verse();
        PlacementV placement = lyrics->placement();
        PropertyFlags pFlags = lyrics->propertyFlags(Pid::PLACEMENT);
        FontStyle fStyle = lyrics->fontStyle();
        PropertyFlags fFlags = lyrics->propertyFlags(Pid::FONT_STYLE);
        TextStyleType styleType = lyrics->textStyleType();

        Segment* nextSegment = segment;
        while ((nextSegment = nextSegment->next1(SegmentType::ChordRest))) {
            EngravingItem* el = nextSegment->element(track);
            if (!el || !el->isChord()) {
                const track_idx_t strack = trackZeroVoice(track);
                const track_idx_t etrack = strack + VOICES;
                for (track_idx_t t = strack; t < etrack; ++t) {
                    el = nextSegment->element(t);
                    if (el && el->isChord() && toChord(el)->lyrics(verse, placement)) {
                        toLyricTrack = t;
                        break;
                    }
                }
            }
            if (el && el->isChord()) {
                break;
            }
        }
        if (!segmentsAreAdjacent(segment, nextSegment)) {
            nextSegment = nullptr;
        }
        if (!nextSegment && !hasFollowingRepeat && !hasPrecedingRepeat) {
            return;
        }
        PartialLyricsLine* prevPartialLyricsLine = findPrevPartialLyricsLineDash(lyrics);
        endEditText();
        Lyrics* fromLyrics = nullptr;
        Segment* curSeg = segment;
        while (segment && segmentsAreAdjacent(segment, curSeg)) {
            ChordRest* cr = toChordRest(segment->element(track));
            if (!cr) {
                segment = segment->prev1(SegmentType::ChordRest);
                continue;
            }
            fromLyrics = cr->lyrics(verse, placement);
            if (fromLyrics) {
                break;
            }
            segment = segment->prev1(SegmentType::ChordRest);
        }
        if (!nextSegment) {
            m_s->startCmd(TranslatableString::untranslatable("Navigate to next syllable"));
            if (fromLyrics && hasFollowingRepeat) {
                switch (fromLyrics->syllabic()) {
                case LyricsSyllabic::BEGIN:
                case LyricsSyllabic::MIDDLE:
                    break;
                case LyricsSyllabic::SINGLE:
                    fromLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::BEGIN));
                    break;
                case LyricsSyllabic::END:
                    fromLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::MIDDLE));
                    break;
                }
                fromLyrics->undoChangeProperty(Pid::LYRIC_TICKS, Fraction(0, 1));
                m_s->setLayoutAll();
                m_s->endCmd();
                return;
            }
            if (hasPrecedingRepeat) {
                m_s->endCmd();
                m_s->startCmd(TranslatableString::untranslatable("Add partial lyrics dash"));
                PartialLyricsLine* dash = Factory::createPartialLyricsLine(m_s->dummy());
                dash->setIsEndMelisma(false);
                dash->setVerse(verse);
                dash->setPlacement(placement);
                dash->setTick(initialCR->tick());
                dash->setTicks(Fraction(0, 1));
                dash->setTrack(initialCR->track());
                dash->setTrack2(initialCR->track());
                m_s->undoAddElement(dash);
                Lyrics* toLyrics = Factory::createLyrics(initialCR);
                toLyrics->setTrack(track);
                toLyrics->setParent(initialCR);
                toLyrics->setVerse(verse);
                toLyrics->setTextStyleType(styleType);
                toLyrics->setPlacement(placement);
                toLyrics->setPropertyFlags(Pid::PLACEMENT, pFlags);
                toLyrics->setSyllabic(LyricsSyllabic::END);
                toLyrics->setFontStyle(fStyle);
                toLyrics->setPropertyFlags(Pid::FONT_STYLE, fFlags);
                m_s->undoAddElement(toLyrics);
                m_s->setLayoutAll();
                m_s->endCmd();
                startEditText(toLyrics, true);
                return;
            }
            m_s->endCmd();
            return;
        }
        m_s->startCmd(TranslatableString::untranslatable("Navigate to next syllable"));
        ChordRest* cr = toChordRest(nextSegment->element(toLyricTrack));
        Lyrics* toLyrics = cr ? cr->lyrics(verse, placement) : nullptr;
        if (!toLyrics && !cr) {
            m_s->endCmd();
            return;
        }
        if (!toLyrics) {
            const track_idx_t strack = trackZeroVoice(track);
            const track_idx_t etrack = strack + VOICES;
            for (track_idx_t t = strack; t < etrack; ++t) {
                if (t == track) {
                    continue;
                }
                cr = toChordRest(nextSegment->element(t));
                if (cr) {
                    toLyrics = cr->lyrics(verse, placement);
                    if (toLyrics) {
                        break;
                    }
                }
            }
        }
        toLyrics = hasPrecedingRepeat && !fromLyrics ? nullptr : toLyrics;
        cr = !toLyrics ? toChordRest(nextSegment->element(track)) : cr;
        if (cr && fromLyrics) {
            Measure* toLyricsMeasure = cr->measure();
            Measure* fromLyricsMeasure = fromLyrics->measure();
            if (toLyricsMeasure != fromLyricsMeasure && fromLyricsMeasure->lastChordRest(track)->hasFollowingJumpItem()) {
                const std::vector<Measure*> previousRepeats = findPreviousRepeatMeasures(toLyricsMeasure);
                const bool inPrecedingRepeatSeg = std::find(previousRepeats.begin(), previousRepeats.end(), fromLyricsMeasure)
                                                  != previousRepeats.end();
                if (!previousRepeats.empty() && !inPrecedingRepeatSeg) {
                    fromLyrics = nullptr;
                }
            }
        }
        const bool startIncomingPartialDash = hasPrecedingRepeat && !fromLyrics && !prevPartialLyricsLine && lyrics->xmlText().empty();
        const bool newLyrics = !toLyrics || startIncomingPartialDash;
        if (newLyrics) {
            ChordRest* toLyricsChord = startIncomingPartialDash ? initialCR : cr;
            if (!toLyricsChord) {
                m_s->endCmd();
                return;
            }
            toLyrics = Factory::createLyrics(toLyricsChord);
            toLyrics->setTrack(track);
            toLyrics->setParent(toLyricsChord);
            toLyrics->setVerse(verse);
            toLyrics->setTextStyleType(styleType);
            toLyrics->setPlacement(placement);
            toLyrics->setPropertyFlags(Pid::PLACEMENT, pFlags);
            toLyrics->setSyllabic(LyricsSyllabic::END);
            toLyrics->setFontStyle(fStyle);
            toLyrics->setPropertyFlags(Pid::FONT_STYLE, fFlags);
        } else {
            if (toLyrics->syllabic() == LyricsSyllabic::BEGIN) {
                toLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::MIDDLE));
            } else if (toLyrics->syllabic() == LyricsSyllabic::SINGLE) {
                toLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::END));
            }
        }
        if (fromLyrics) {
            switch (fromLyrics->syllabic()) {
            case LyricsSyllabic::BEGIN:
            case LyricsSyllabic::MIDDLE:
                break;
            case LyricsSyllabic::SINGLE:
                fromLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::BEGIN));
                break;
            case LyricsSyllabic::END:
                fromLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::MIDDLE));
                break;
            }
            fromLyrics->undoChangeProperty(Pid::LYRIC_TICKS, Fraction(0, 1));
        } else if (hasPrecedingRepeat && !prevPartialLyricsLine) {
            PartialLyricsLine* dash = Factory::createPartialLyricsLine(m_s->dummy());
            dash->setIsEndMelisma(false);
            dash->setVerse(verse);
            dash->setPlacement(lyrics->placement());
            dash->setTick(initialCR->tick());
            dash->setTicks(hasPrecedingRepeat ? Fraction(0, 1) : initialCR->ticks());
            dash->setTrack(initialCR->track());
            dash->setTrack2(initialCR->track());
            m_s->undoAddElement(dash);
        } else if (prevPartialLyricsLine) {
            const Fraction tickDiff = cr->tick() - prevPartialLyricsLine->tick2();
            prevPartialLyricsLine->undoMoveEnd(tickDiff);
            m_s->undoAddElement(prevPartialLyricsLine);
            prevPartialLyricsLine->triggerLayout();
        }
        if (newLyrics) {
            m_s->undoAddElement(toLyrics);
        }
        m_s->setLayoutAll();
        m_s->endCmd();
        startEditText(toLyrics, newLyrics);
    }

    void addMelisma()
    {
        Lyrics* lyrics = cur;
        ChordRest* initialCR = lyrics->chordRest();
        const bool hasPrecedingRepeat = initialCR->hasPrecedingJumpItem();
        track_idx_t track = lyrics->track();
        Segment* segment = lyrics->segment();
        int verse = lyrics->verse();
        PlacementV placement = lyrics->placement();
        PropertyFlags pFlags = lyrics->propertyFlags(Pid::PLACEMENT);
        FontStyle fStyle = lyrics->fontStyle();
        PropertyFlags fFlags = lyrics->propertyFlags(Pid::FONT_STYLE);
        Fraction endTick = segment->tick();
        endEditText();

        Segment* nextSegment = segment;
        while ((nextSegment = nextSegment->next1(SegmentType::ChordRest))) {
            EngravingItem* el = nextSegment->element(track);
            if (el && el->isChord()) {
                break;
            }
        }
        if (!segmentsAreAdjacent(segment, nextSegment)) {
            nextSegment = nullptr;
        }
        Segment* curSeg = segment;
        Lyrics* fromLyrics = nullptr;
        PartialLyricsLine* prevPartialLyricsLine = nullptr;
        while (segment && segmentsAreAdjacent(segment, curSeg)) {
            ChordRest* cr = toChordRest(segment->element(track));
            if (cr) {
                fromLyrics = cr->lyrics(verse, placement);
                if (fromLyrics) {
                    break;
                }
                auto spanners = m_s->spannerMap().findOverlapping(cr->tick().ticks(), cr->endTick().ticks());
                for (auto& spanner : spanners) {
                    if (!spanner.value->isPartialLyricsLine() || spanner.value->staffIdx() != cr->staffIdx()) {
                        continue;
                    }
                    PartialLyricsLine* lyricsLine = toPartialLyricsLine(spanner.value);
                    if (lyricsLine->verse() != verse || lyricsLine->placement() != placement || !lyricsLine->isEndMelisma()) {
                        continue;
                    }
                    prevPartialLyricsLine = lyricsLine;
                    break;
                }
                if (prevPartialLyricsLine) {
                    break;
                }
            }
            segment = segment->prev1(SegmentType::ChordRest);
            EngravingItem* e = segment ? segment->element(track) : 0;
            if (e && !e->isChord()) {
                break;
            }
        }
        if (fromLyrics == lyrics && nextSegment) {
            if (std::getenv("JUBELLE_REFERENCE_DEBUG")) {
                std::cerr << "  temp: before sep=" << (lyrics->separator() != nullptr) << " need=" << lyrics->needRemoveInvalidSegments() << "\n";
            }
            m_s->startCmd(TranslatableString::untranslatable("Enter lyrics extension line"));
            lyrics->undoChangeProperty(Pid::LYRIC_TICKS, Lyrics::TEMP_MELISMA_TICKS);
            if (std::getenv("JUBELLE_REFERENCE_DEBUG")) {
                std::cerr << "  temp: set need=" << lyrics->needRemoveInvalidSegments() << " ticks=" << frac(lyrics->ticks()) << "\n";
            }
            m_s->setLayoutAll();
            m_s->endCmd();
            if (std::getenv("JUBELLE_REFERENCE_DEBUG")) {
                std::cerr << "  temp: after ticks=" << frac(lyrics->ticks()) << " syl=" << int(lyrics->syllabic()) << " sep=" << (lyrics->separator() != nullptr) << "\n";
            }
        }
        auto getPartialLyricLineEndSegment = [&]() -> Fraction {
            Segment* endSegment = segment;
            while (endSegment && endSegment->tick() < endTick) {
                endSegment = endSegment->nextCR(track, true);
            }
            if (!endSegment) {
                return m_s->endTick();
            }
            EngravingItem* endSegmentElement = endSegment->element(track);
            if (endSegment->tick() == endTick && endSegmentElement && endSegmentElement->isChord()) {
                Segment* endChordSeg = endSegment;
                Chord* endChord = toChord(endSegmentElement);
                endSegment = endChordSeg->nextCR(track, false);
                if (!endSegment || endSegment->tick() > endChord->endTick()) {
                    endSegment = endChordSeg;
                    while (endSegment && endSegment->tick() < endChord->endTick()) {
                        endSegment = endSegment->nextCR(muse::nidx, true);
                    }
                }
            }
            return endSegment ? endSegment->tick() : m_s->endTick();
        };
        if (!nextSegment) {
            m_s->startCmd(TranslatableString::untranslatable("Enter lyrics extension line"));
            if (fromLyrics) {
                switch (fromLyrics->syllabic()) {
                case LyricsSyllabic::SINGLE:
                case LyricsSyllabic::END:
                    break;
                default:
                    fromLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::END));
                    break;
                }
                if (fromLyrics->segment()->tick() < endTick
                    || (segment && endTick + initialCR->actualTicks() == segment->measure()->endTick() && initialCR->hasFollowingJumpItem())) {
                    Fraction ticks = std::max(endTick - fromLyrics->segment()->tick(), Lyrics::TEMP_MELISMA_TICKS);
                    fromLyrics->undoChangeProperty(Pid::LYRIC_TICKS, ticks);
                }
            }
            if (hasPrecedingRepeat && !prevPartialLyricsLine) {
                PartialLyricsLine* melisma = Factory::createPartialLyricsLine(m_s->dummy());
                melisma->setIsEndMelisma(true);
                melisma->setVerse(verse);
                melisma->setPlacement(placement);
                melisma->setTick(initialCR->tick());
                melisma->setTicks(initialCR->ticks());
                melisma->setTrack(initialCR->track());
                melisma->setTrack2(initialCR->track());
                m_s->undoAddElement(melisma);
            } else if (prevPartialLyricsLine) {
                const Fraction segEndTick = getPartialLyricLineEndSegment();
                const Fraction tickDiff = segEndTick - prevPartialLyricsLine->tick2();
                prevPartialLyricsLine->undoMoveEnd(tickDiff);
            }
            m_s->setLayoutAll();
            m_s->endCmd();
            return;
        }
        ChordRest* nextCR = toChordRest(nextSegment->element(track));
        if (nextCR && fromLyrics) {
            Measure* toLyricsMeasure = nextCR->measure();
            Measure* fromLyricsMeasure = fromLyrics->measure();
            if (toLyricsMeasure != fromLyricsMeasure && fromLyricsMeasure->lastChordRest(track)->hasFollowingJumpItem()) {
                const std::vector<Measure*> previousRepeats = findPreviousRepeatMeasures(toLyricsMeasure);
                const bool inPrecedingRepeatSeg = std::find(previousRepeats.begin(), previousRepeats.end(), fromLyricsMeasure)
                                                  != previousRepeats.end();
                if (!previousRepeats.empty() && !inPrecedingRepeatSeg) {
                    fromLyrics = nullptr;
                }
            }
        }
        m_s->startCmd(TranslatableString::untranslatable("Enter lyrics extension line"));
        Lyrics* toLyrics = nextCR->lyrics(verse, placement);
        bool newLyrics = (toLyrics == nullptr);
        if (!toLyrics) {
            toLyrics = Factory::createLyrics(nextCR);
            toLyrics->setTrack(track);
            toLyrics->setParent(nextCR);
            toLyrics->setVerse(verse);
            const TextStyleType styleType(toLyrics->isEven() ? TextStyleType::LYRICS_EVEN : TextStyleType::LYRICS_ODD);
            toLyrics->setTextStyleType(styleType);
            toLyrics->setPlacement(placement);
            toLyrics->setPropertyFlags(Pid::PLACEMENT, pFlags);
            toLyrics->setSyllabic(LyricsSyllabic::SINGLE);
            toLyrics->setFontStyle(fStyle);
            toLyrics->setPropertyFlags(Pid::FONT_STYLE, fFlags);
        } else if (toLyrics->syllabic() == LyricsSyllabic::MIDDLE) {
            toLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::BEGIN));
        } else if (toLyrics->syllabic() == LyricsSyllabic::END) {
            toLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::SINGLE));
        }
        if (fromLyrics) {
            switch (fromLyrics->syllabic()) {
            case LyricsSyllabic::SINGLE:
            case LyricsSyllabic::END:
                break;
            default:
                fromLyrics->undoChangeProperty(Pid::SYLLABIC, int(LyricsSyllabic::END));
                break;
            }
            if (fromLyrics->segment()->tick() < endTick) {
                fromLyrics->undoChangeProperty(Pid::LYRIC_TICKS, endTick - fromLyrics->segment()->tick());
            }
        } else if (hasPrecedingRepeat && !prevPartialLyricsLine) {
            PartialLyricsLine* melisma = Factory::createPartialLyricsLine(m_s->dummy());
            melisma->setIsEndMelisma(true);
            melisma->setVerse(verse);
            melisma->setPlacement(placement);
            melisma->setTick(initialCR->tick());
            melisma->setTicks(initialCR->ticks());
            melisma->setTrack(initialCR->track());
            melisma->setTrack2(initialCR->track());
            m_s->undoAddElement(melisma);
        } else if (prevPartialLyricsLine) {
            const Fraction segEndTick = getPartialLyricLineEndSegment();
            const Fraction tickDiff = segEndTick - prevPartialLyricsLine->tick2();
            prevPartialLyricsLine->undoMoveEnd(tickDiff);
            prevPartialLyricsLine->triggerLayout();
        }
        m_s->setLayoutAll();
        m_s->endCmd();
        m_s->startCmd(TranslatableString::untranslatable("Enter lyrics extension line"));
        if (newLyrics) {
            m_s->undoAddElement(toLyrics);
        }
        m_s->setLayoutAll();
        m_s->endCmd();
        startEditText(toLyrics, newLyrics);
    }

    void addLyricsVerse()
    {
        Lyrics* oldLyrics = cur;
        FontStyle fStyle = oldLyrics->fontStyle();
        PropertyFlags fFlags = oldLyrics->propertyFlags(Pid::FONT_STYLE);
        ChordRest* cr = oldLyrics->chordRest();
        const int newVerse = oldLyrics->verse() + 1;
        const PlacementV placement = oldLyrics->placement();
        const PropertyFlags pFlags = oldLyrics->propertyFlags(Pid::PLACEMENT);
        const track_idx_t track = oldLyrics->track();
        endEditText();
        if (Lyrics* existing = cr->lyrics(newVerse, placement)) {
            // as the engine does (an intentional difference): MuseScore would add a second lyric of the verse
            startEditText(existing, false);
            return;
        }
        m_s->startCmd(TranslatableString::untranslatable("Add lyrics verse"));
        Lyrics* lyrics = Factory::createLyrics(cr);
        lyrics->setTrack(track);
        lyrics->setParent(cr);
        lyrics->setPlacement(placement);
        lyrics->setPropertyFlags(Pid::PLACEMENT, pFlags);
        lyrics->setVerse(newVerse);
        const TextStyleType styleType(lyrics->isEven() ? TextStyleType::LYRICS_EVEN : TextStyleType::LYRICS_ODD);
        lyrics->setTextStyleType(styleType);
        lyrics->setFontStyle(fStyle);
        lyrics->setPropertyFlags(Pid::FONT_STYLE, fFlags);
        m_s->undoAddElement(lyrics);
        m_s->setLayoutAll();
        m_s->endCmd();
        startEditText(lyrics, true);
    }

    void navigateToLyricsVerse(bool up)
    {
        Lyrics* lyrics = cur;
        track_idx_t track = lyrics->track();
        ChordRest* cr = lyrics->chordRest();
        int verse = lyrics->verse();
        PlacementV placement = lyrics->placement();
        PropertyFlags pFlags = lyrics->propertyFlags(Pid::PLACEMENT);
        FontStyle fStyle = lyrics->fontStyle();
        PropertyFlags fFlags = lyrics->propertyFlags(Pid::FONT_STYLE);
        TextStyleType styleType = lyrics->textStyleType();
        if (up) {
            if (verse == 0) {
                return;
            }
            --verse;
        } else {
            ++verse;
            if (verse > cr->lastVerse(placement)) {
                return;
            }
        }
        endEditText();
        lyrics = cr->lyrics(verse, placement);
        bool made = false;
        if (!lyrics) {
            lyrics = Factory::createLyrics(cr);
            lyrics->setTrack(track);
            lyrics->setParent(cr);
            lyrics->setVerse(verse);
            lyrics->setTextStyleType(styleType);
            lyrics->setPlacement(placement);
            lyrics->setPropertyFlags(Pid::PLACEMENT, pFlags);
            lyrics->setFontStyle(fStyle);
            lyrics->setPropertyFlags(Pid::FONT_STYLE, fFlags);
            m_s->startCmd(TranslatableString::untranslatable("Navigate to verse"));
            m_s->undoAddElement(lyrics);
            m_s->setLayoutAll();
            m_s->endCmd();
            made = true;
        }
        startEditText(lyrics, made);
    }

    void run(const std::string& script)
    {
        auto key = [&](char k) {
            if (!cur) {
                typed.clear();
                return;
            }
            switch (k) {
            case '-': navigateToNextSyllable(); break;
            case '_': addMelisma(); break;
            case ' ': navigateToLyrics(false, false); break;
            case '<': navigateToLyrics(true, false); break;
            case '/': addLyricsVerse(); break;
            case '^': navigateToLyricsVerse(true); break;
            case '!': navigateToLyricsVerse(false); break;
            default: break;
            }
        };
        for (char c : script) {
            if (std::string("-_ </^!").find(c) != std::string::npos) {
                key(c);
                if (std::getenv("JUBELLE_REFERENCE_DEBUG")) {
                    std::cerr << "after '" << c << "':";
                    for (Segment* seg = m_s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                        for (EngravingItem* e : seg->elist()) {
                            if (e && e->isChordRest()) {
                                for (Lyrics* l : toChordRest(e)->lyrics()) {
                                    std::cerr << " " << frac(seg->tick()) << "/" << l->track() << ":" << int(l->syllabic()) << ":" << frac(l->ticks())
                                              << ":" << l->xmlText().toStdString();
                                }
                            }
                        }
                    }
                    std::cerr << (cur ? " editing " + frac(cur->tick()) : std::string(" -")) << "\n";
                }
            } else if (cur) {
                typed += c;
            }
        }
        endEditText();
    }

private:
    MasterScore* m_s;
};
}

TEST(Jubelle_Reference, MusicListing)
{
    const char* root = std::getenv("JUBELLE_REFERENCE_ROOT");
    const char* list = std::getenv("JUBELLE_REFERENCE_LIST");
    const char* outDir = std::getenv("JUBELLE_REFERENCE_OUT");
    if (!root || !list || !outDir) {
        GTEST_SKIP() << "set JUBELLE_REFERENCE_ROOT, JUBELLE_REFERENCE_LIST and JUBELLE_REFERENCE_OUT";
    }
    std::ifstream in(list);
    int done = 0, failed = 0;
    for (std::string rel; std::getline(in, rel);) {
        if (rel.empty()) {
            continue;
        }
        const std::string path = std::string(root) + "/" + rel;
        MasterScore* s = compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr);
        s->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(muse::io::path_t(path)));
        const std::filesystem::path out = std::filesystem::path(outDir) / (rel + ".txt");
        std::filesystem::create_directories(out.parent_path());
        if (!compat::loadMsczOrMscx(s, muse::io::path_t(path), false)) {
            std::ofstream(out) << "NOT READ\n";
            ++failed;
        } else {
            std::string head;
            if (const char* ts = std::getenv("JUBELLE_REFERENCE_TIMESIG")) {
                std::istringstream spec(ts);
                int index = 0, num = 4, den = 4;
                spec >> index >> num >> den;
                Measure* m = s->firstMeasure();
                for (int i = 0; m && i < index; ++i) {
                    m = m->nextMeasure();
                }
                bool changed = false;
                if (m) {
                    // laid out first, as a score in the app always is (the measures' end barline segments come
                    // from the layout, and the rewrite reads up to them)
                    s->setLayoutAll();
                    s->doLayout();
                    // in the app every measure ends with its end barline segment (made by the layout, which here, without
                    // a page view, doesn't add them); the rewrite reads up to it, so it is made here too
                    for (Measure* mm = s->firstMeasure(); mm; mm = mm->nextMeasure()) {
                        mm->getSegmentR(SegmentType::EndBarLine, mm->ticks());
                    }
                    if (std::getenv("JUBELLE_REFERENCE_DEBUG")) {
                        for (Measure* mm = s->firstMeasure(); mm; mm = mm->nextMeasure()) {
                            std::cout << "measure " << frac(mm->tick()) << " +" << frac(mm->ticks()) << ":";
                            for (Segment* sg = mm->first(); sg; sg = sg->next()) {
                                std::cout << " " << sg->subTypeName() << "@" << frac(sg->tick());
                            }
                            std::cout << "\n";
                        }
                    }
                    const std::string before = listing(s);
                    TimeSig* sig = Factory::createTimeSig(s->dummy()->segment());
                    sig->setSig(Fraction(num, den), TimeSigType::NORMAL);
                    const auto started = std::chrono::steady_clock::now();
                    s->startCmd(TranslatableString::untranslatable("time signature"));
                    s->cmdAddTimeSig(m, 0, sig, false);
                    s->endCmd();
                    if (std::getenv("JUBELLE_REFERENCE_TIME")) {
                        std::cout << "time: " << rel << " "
                                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << " ms\n";
                    }
                    Measure* after = s->firstMeasure();   // the measures may be new ones now
                    for (int i = 0; after && i < index; ++i) {
                        after = after->nextMeasure();
                    }
                    changed = listing(s) != before || (after && after->timesig().identical(Fraction(num, den)));
                }
                head = std::string("# timesig ") + (changed ? "changed" : "refused") + "\n";
            }
            const char* explode = std::getenv("JUBELLE_REFERENCE_EXPLODE");
            const char* implode = std::getenv("JUBELLE_REFERENCE_IMPLODE");
            const char* slashFill = std::getenv("JUBELLE_REFERENCE_SLASHFILL");
            const char* slashRhythm = std::getenv("JUBELLE_REFERENCE_SLASHRHYTHM");
            const char* realize = std::getenv("JUBELLE_REFERENCE_REALIZE");
            const char* moveStaff = std::getenv("JUBELLE_REFERENCE_MOVESTAFF");
            if (explode || implode || slashFill || slashRhythm || realize || moveStaff) {
                // "<first staff> <last staff (exclusive)> <first measure> <last measure>": that range selected, then
                // Score::cmdExplode / cmdImplode / cmdSlashFill / cmdSlashRhythm
                if (slashFill || slashRhythm || realize || moveStaff) {
                    // measures as they are: a selection ending in a multimeasure rest would take in the whole rest
                    // (the selection is the interface's matter, not the command's)
                    s->style().set(Sid::createMultiMeasureRests, false);
                }
                s->setLayoutAll();
                s->doLayout();
                const std::string before = listing(s);
                std::istringstream spec(explode ? explode : (implode ? implode : (slashFill ? slashFill : (slashRhythm ? slashRhythm : (realize ? realize : moveStaff)))));
                int s0 = 0, s1 = 1, m0 = 0, m1 = 0;
                spec >> s0 >> s1 >> m0 >> m1;
                int voicing = -1, duration = -1, literal = 1;   // realize: the chord symbols' own settings unless given
                spec >> voicing >> duration >> literal;
                auto measureAt = [s](int index) {
                    Measure* mm = s->firstMeasure();
                    for (int i = 0; mm && i < index; ++i) {
                        mm = mm->nextMeasure();
                    }
                    return mm;
                };
                Measure* ma = measureAt(m0);
                Measure* mb = measureAt(m1);
                const bool can = ma && mb && size_t(s1) <= s->nstaves() && s0 < s1;
                if (can) {
                    s->startCmd(TranslatableString::untranslatable("explode"));
                    s->select(ma, SelectType::RANGE, size_t(s0));
                    s->select(mb, SelectType::RANGE, size_t(s1 - 1));
                    if (explode) {
                        s->cmdExplode();
                    } else if (implode) {
                        s->cmdImplode();
                    } else if (slashFill) {
                        s->cmdSlashFill();
                    } else if (slashRhythm) {
                        s->cmdSlashRhythm();
                    } else if (moveStaff) {
                        // NotationInteraction::moveChordRestToStaff: each selected chord or rest, up (0) or down (1)
                        for (EngravingItem* e : s->selection().uniqueElements()) {
                            ChordRest* cr = e->isChordRest() ? toChordRest(e) : (e->isNote() ? toNote(e)->chord() : nullptr);
                            if (cr) {
                                if (voicing == 0) {
                                    s->moveUp(cr);
                                } else {
                                    s->moveDown(cr);
                                }
                            }
                        }
                    } else {
                        s->cmdRealizeChordSymbols(literal != 0, Voicing(voicing), HDuration(duration));
                    }
                    s->endCmd();
                }
                head = std::string("# measures ") + (can && listing(s) != before ? "changed" : (can ? "unchanged" : "refused")) + "\n";
            }
            if (const char* lyricsSpec = std::getenv("JUBELLE_REFERENCE_LYRICS")) {
                // "<staff> <measure> <keys>": lyrics typed from the first chord or rest of the measure's voice 1
                // (Ctrl+L there: Score::addText(LYRICS_ODD)), or (measure -1) editing the staff's first lyric
                s->style().set(Sid::createMultiMeasureRests, false);
                s->setPlaylistDirty();
                s->setLayoutAll();
                s->doLayout();
                const std::string before = listing(s);
                std::istringstream spec(lyricsSpec);
                int staffNo = 0, measureNo = 0;
                spec >> staffNo >> measureNo;
                std::string keys;
                std::getline(spec, keys);
                if (!keys.empty() && keys[0] == ' ') {
                    keys.erase(0, 1);
                }
                const track_idx_t track = size_t(staffNo) * VOICES;
                LyricSession session(s);
                bool can = false;
                if (size_t(staffNo) < s->nstaves()) {
                    Measure* m = s->firstMeasure();
                    for (int i = 0; m && i < measureNo; ++i) {
                        m = m->nextMeasure();
                    }
                    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                        EngravingItem* e = seg->element(track);
                        if (!e || !e->isChordRest()) {
                            continue;
                        }
                        ChordRest* cr = toChordRest(e);
                        if (measureNo >= 0 && m && seg->measure() == m) {
                            EngravingItem* dest = cr->isChord() ? static_cast<EngravingItem*>(toChord(cr)->notes().front()) : cr;
                            s->startCmd(TranslatableString::untranslatable("add lyrics"));
                            TextBase* t = s->addText(TextStyleType::LYRICS_ODD, dest);
                            s->endCmd();
                            if (t) {
                                session.startEditText(toLyrics(t), true);
                                can = true;
                            }
                            break;
                        }
                        if (measureNo < 0 && !cr->lyrics().empty()) {
                            session.startEditText(cr->lyrics().front(), false);
                            can = true;
                            break;
                        }
                    }
                }
                if (can) {
                    session.run(keys);
                }
                head = std::string("# measures ") + (can && listing(s) != before ? "changed" : (can ? "unchanged" : "refused")) + "\n";
            }
            if (const char* textSpec = std::getenv("JUBELLE_REFERENCE_ADDTEXT")) {
                // "<staff> <measure> <style> <text>": Score::addText(style) on the first chord or rest of the measure's
                // voice 1 (its top note), then the text typed (TextBase::endEdit: an empty text removes the element)
                s->style().set(Sid::createMultiMeasureRests, false);
                s->setLayoutAll();
                s->doLayout();
                const std::string before = listing(s);
                std::istringstream spec(textSpec);
                int staffNo = 0, measureNo = 0;
                std::string style, text;
                spec >> staffNo >> measureNo >> style;
                std::getline(spec, text);
                if (!text.empty() && text[0] == ' ') {
                    text.erase(0, 1);
                }
                bool can = false;
                Measure* m = s->firstMeasure();
                for (int i = 0; m && i < measureNo; ++i) {
                    m = m->nextMeasure();
                }
                if (m && size_t(staffNo) < s->nstaves()) {
                    for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                        EngravingItem* e = seg->element(size_t(staffNo) * VOICES);
                        if (!e || !e->isChordRest()) {
                            continue;
                        }
                        ChordRest* cr = toChordRest(e);
                        EngravingItem* dest = cr->isChord() ? static_cast<EngravingItem*>(toChord(cr)->upNote()) : cr;
                        s->startCmd(TranslatableString::untranslatable("add text"));
                        TextBase* t = s->addText(TConv::fromXml(muse::AsciiStringView(style.c_str()), TextStyleType::STAFF), dest);
                        s->endCmd();
                        if (t) {
                            can = true;
                            const String typed = String::fromStdString(text);
                            if (!typed.isEmpty()) {
                                if (typed != t->xmlText()) {
                                    s->startCmd(TranslatableString::untranslatable("text"));
                                    t->undoChangeProperty(Pid::TEXT, typed);
                                    if (t->isDynamic()) {
                                        t->undoChangeProperty(Pid::DYNAMIC_TYPE, typed);
                                    }
                                    s->endCmd();
                                }
                            } else if (t->plainText().isEmpty()) {
                                s->undoRedo(true, nullptr);   // left empty: the command that made it rolled back
                                can = false;
                            }
                        }
                        break;
                    }
                }
                head = std::string("# measures ") + (can && listing(s) != before ? "changed" : (can ? "unchanged" : "refused")) + "\n";
            }
            const char* split = std::getenv("JUBELLE_REFERENCE_SPLIT");
            const char* join = std::getenv("JUBELLE_REFERENCE_JOIN");
            if (split || join) {
                s->setLayoutAll();
                s->doLayout();
                for (Measure* mm = s->firstMeasure(); mm; mm = mm->nextMeasure()) {
                    mm->getSegmentR(SegmentType::EndBarLine, mm->ticks());
                }
                const std::string before = listing(s);
                std::istringstream spec(split ? split : join);
                int a = 0, b = 0;
                spec >> a >> b;
                auto measureAt = [s](int index) {
                    Measure* mm = s->firstMeasure();
                    for (int i = 0; mm && i < index; ++i) {
                        mm = mm->nextMeasure();
                    }
                    return mm;
                };
                Measure* ma = measureAt(a);
                Measure* mb = join ? measureAt(b) : nullptr;
                bool can = ma && (split || (mb && a < b));
                if (can && split) {
                    const Fraction tick = ma->tick() + ma->ticks() * Fraction(1, 2);
                    can = s->tick2segment(tick, false, SegmentType::ChordRest) != nullptr;
                    if (can) {
                        s->startCmd(TranslatableString::untranslatable("split"));
                        SplitJoinMeasure::splitMeasure(s, tick);
                        s->endCmd();
                    }
                } else if (can) {
                    s->startCmd(TranslatableString::untranslatable("join"));
                    SplitJoinMeasure::joinMeasures(s, ma->tick(), mb->tick());
                    s->endCmd();
                }
                head = std::string("# measures ") + (can && listing(s) != before ? "changed" : (can ? "unchanged" : "refused")) + "\n";
            }
            if (std::getenv("JUBELLE_REFERENCE_REPEATS")) {
                // the play order (MuseScore's RepeatList, repeats expanded): measure index ranges and pass
                std::map<const Measure*, int> index;
                int n = 0;
                for (Measure* m = s->firstMeasure(); m; m = m->nextMeasure()) {
                    index[m] = n++;
                }
                s->setPlaylistDirty();   // as the app does on opening (reading may have cached a partial list)
                s->doLayout();   // as MuseScore's repeat tests read scores (voltas are measured by layout)
                std::string line = "# repeats:";
                for (const RepeatSegment* rs : s->repeatList(true, false)) {
                    if (!rs->firstMeasure()) {
                        continue;
                    }
                    line += " " + std::to_string(index[rs->firstMeasure()]) + "-" + std::to_string(index[rs->lastMeasure()])
                            + "/" + std::to_string(rs->playbackCount);
                }
                head += line + "\n";
            }
            if (const char* saveDir = std::getenv("JUBELLE_REFERENCE_SAVE")) {
                // MuseScore's own save (4.x), to turn older test files into files the engine reads
                std::filesystem::path to = std::filesystem::path(saveDir) / rel;
                to.replace_extension(".mscx");
                std::filesystem::create_directories(to.parent_path());
                ScoreRW::saveScore(s, String::fromStdString(to.string()));
            }
            std::ofstream(out) << head << listing(s);
            ++done;
        }
        delete s;
    }
    std::cout << "reference listings: " << done << " read, " << failed << " not read\n";
}

//! MuseScore's style values and their defaults, as MuseScore writes them in a file, for the Jubelle engine's style
//! table (je::style, generated by tools/jubelle_checks/je_gen_style.py): one line per value, "name TAB type TAB xml"
TEST(Jubelle_Reference, StyleDefaults)
{
    const char* out = std::getenv("JUBELLE_REFERENCE_STYLEDEFS");
    if (!out) {
        GTEST_SKIP() << "set JUBELLE_REFERENCE_STYLEDEFS";
    }
    // the definitions' own defaults (DefaultStyle::defaultStyle() may already carry the program's page settings)
    MStyle style = DefaultStyle::defaultStyle();
    for (const StyleDef::StyleValue& st : StyleDef::styleValues) {
        style.set(st.sid, st.defaultValue);
    }
    muse::io::Buffer buffer;
    buffer.open(muse::io::IODevice::WriteOnly);
    XmlWriter xml(&buffer);
    style.save(xml, false);
    xml.flush();
    std::ofstream o(out);
    o << "#style\n" << buffer.data().constChar() << "\n#types\n";
    for (const StyleDef::StyleValue& st : StyleDef::styleValues) {
        o << st.xmlName.ascii() << "\t" << int(st.valueType()) << "\t";
        // numbers exactly (the file rounds them to 6 digits)
        char buf[64] = "";
        switch (st.valueType()) {
        case P_TYPE::REAL:
            std::snprintf(buf, sizeof(buf), "%.17g", st.defaultValue.toReal());
            break;
        case P_TYPE::SPATIUM:
            std::snprintf(buf, sizeof(buf), "%.17g", st.defaultValue.value<Spatium>().val());
            break;
        case P_TYPE::MILLIMETRE:
            std::snprintf(buf, sizeof(buf), "%.17g", double(st.defaultValue.value<Millimetre>()));
            break;
        case P_TYPE::POINT: {
            const PointF pt = st.defaultValue.value<PointF>();
            std::snprintf(buf, sizeof(buf), "%.17g %.17g", pt.x(), pt.y());
        } break;
        default:
            break;
        }
        o << buf << "\n";
    }
    // the tables of MuseScore's style compatibility (CompatUtils::setPositionStylesFromAlign, setMusicSymbolSize470)
    o << "#align\n";
    for (const auto& [align, position] : compat::CompatUtils::ALIGN_VALS_TO_CONVERT) {
        o << MStyle::valueName(align) << "\t" << MStyle::valueName(position) << "\n";
    }
    o << "#symsize\n";
    for (TextStyleType t : allTextStyles()) {
        if (t == TextStyleType::REPEAT_LEFT || t == TextStyleType::REPEAT_RIGHT) {
            continue;
        }
        const TextStyle* ts = textStyle(t);
        Sid sym = Sid::NOSTYLE, size = Sid::NOSTYLE;
        for (size_t i = 0; i < TEXT_STYLE_SIZE; ++i) {
            if (ts->at(i).pid == Pid::MUSIC_SYMBOL_SIZE) {
                sym = ts->at(i).sid;
            }
            if (ts->at(i).pid == Pid::FONT_SIZE) {
                size = ts->at(i).sid;
            }
        }
        if (sym != Sid::NOSTYLE && size != Sid::NOSTYLE) {
            o << MStyle::valueName(sym) << "\t" << MStyle::valueName(size) << "\t" << (t == TextStyleType::TEMPO ? 1 : 0) << "\n";
        }
    }
    // the notehead table (Note::noteHead): group (as written in files), type, down and up
    o << "#noteheads\n";
    for (int g = 0; g < int(NoteHeadGroup::HEAD_GROUPS) - 1; ++g) {
        o << TConv::toXml(NoteHeadGroup(g)).ascii();
        for (int t = 0; t < int(NoteHeadType::HEAD_TYPES); ++t) {
            for (int d = 0; d < 2; ++d) {
                o << "\t" << SymNames::nameForSymId(Note::noteHead(d, NoteHeadGroup(g), NoteHeadType(t))).ascii();
            }
        }
        o << "\n";
    }
    // the header, footer, copyright and page number text styles, entry by entry (CompatUtils::mapHeaderFooterStyles)
    o << "#headerfooter\n";
    for (size_t i = 0; i < TEXT_STYLE_SIZE; ++i) {
        for (TextStyleType t : { TextStyleType::HEADER, TextStyleType::FOOTER, TextStyleType::COPYRIGHT, TextStyleType::PAGE_NUMBER }) {
            const Sid sid = textStyle(t)->at(i).sid;
            o << (sid == Sid::NOSTYLE ? "NOSTYLE" : MStyle::valueName(sid)) << (t == TextStyleType::PAGE_NUMBER ? "\n" : "\t");
        }
    }
}

//! Every 4.x score's style as MuseScore reads it (all values, as MStyle::save writes them), to compare with the
//! Jubelle engine's (je_io_tool stylelist, je_style_reference.py)
TEST(Jubelle_Reference, StyleListing)
{
    const char* root = std::getenv("JUBELLE_REFERENCE_ROOT");
    const char* list = std::getenv("JUBELLE_REFERENCE_LIST");
    const char* outDir = std::getenv("JUBELLE_REFERENCE_STYLELIST");
    if (!root || !list || !outDir) {
        GTEST_SKIP() << "set JUBELLE_REFERENCE_ROOT, JUBELLE_REFERENCE_LIST and JUBELLE_REFERENCE_STYLELIST";
    }
    // as the application reads styles (the test mode leaves out a compatibility setting)
    const bool testMode = MScore::testMode;
    MScore::testMode = false;
    std::ifstream in(list);
    for (std::string rel; std::getline(in, rel);) {
        if (rel.empty()) {
            continue;
        }
        const std::string path = std::string(root) + "/" + rel;
        MasterScore* s = compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr);
        s->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(muse::io::path_t(path)));
        const std::filesystem::path out = std::filesystem::path(outDir) / (rel + ".style");
        std::filesystem::create_directories(out.parent_path());
        if (!compat::loadMsczOrMscx(s, muse::io::path_t(path), false)) {
            std::ofstream(out) << "NOT READ\n";
        } else {
            muse::io::Buffer buffer;
            buffer.open(muse::io::IODevice::WriteOnly);
            XmlWriter xml(&buffer);
            s->style().save(xml, false);
            xml.flush();
            std::ofstream(out) << buffer.data().constChar();
        }
        delete s;
    }
    MScore::testMode = testMode;
}

namespace {
std::string sp(double v, double spatium)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", v / spatium + 0.0);
    return b;
}

//! One chord's layout as MuseScore computes it: stem direction, its place in the segment, each note's line, place
//! and mirroring, its accidental and dots, the stem's length; in staff spaces (S7's chord shaping comparison)
void chordLayout(std::ostream& o, Chord* c, const std::string& kind)
{
    const double s = c->spatium();
    o << kind << " " << c->staffIdx() << " " << c->voice() << " " << c->tick().ticks() << " up=" << c->up()
      << " x=" << sp(c->pos().x(), s);
    if (c->stem()) {
        o << " stem=" << sp(c->stem()->length(), s);
    }
    if (c->hook()) {
        o << " hook=" << SymNames::nameForSymId(c->hook()->sym()).ascii();
    }
    if (c->beam() && c->beam()->elements().size() > 1) {
        // the beam: where it starts (track and time of its first chord or rest) and how many it joins
        const ChordRest* first = c->beam()->elements().front();
        o << " beam=" << first->track() << ":" << first->tick().ticks() << ":" << c->beam()->elements().size()
          << (first->isGrace() ? "g" : "");
    }
    for (Note* n : c->notes()) {
        o << " | " << n->line() << " " << SymNames::nameForSymId(n->noteHead()).ascii() << " x=" << sp(n->pos().x(), s)
          << " y=" << sp(n->pos().y(), s) << (n->ldata()->mirror() ? " mirror" : "");
        if (Accidental* a = n->accidental()) {
            o << " acc=" << SymNames::nameForSymId(a->symId()).ascii() << "@" << sp(a->pos().x(), s);
        }
        if (!n->dots().empty()) {
            o << " dot=" << sp(n->dots().front()->pos().x(), s) << " doty=" << sp(n->dots().front()->pos().y(), s);
        }
    }
    o << "\n";
}
}

//! Every chord's layout in every listed score, after MuseScore's full layout (JUBELLE_REFERENCE_CHORDS: the output
//! folder), to compare with the Jubelle engine's chord shaping (je_io_tool chordshapes, je_layout_reference.py)
TEST(Jubelle_Reference, ChordLayout)
{
    const char* root = std::getenv("JUBELLE_REFERENCE_ROOT");
    const char* list = std::getenv("JUBELLE_REFERENCE_LIST");
    const char* outDir = std::getenv("JUBELLE_REFERENCE_CHORDS");
    if (!root || !list || !outDir) {
        GTEST_SKIP() << "set JUBELLE_REFERENCE_ROOT, JUBELLE_REFERENCE_LIST and JUBELLE_REFERENCE_CHORDS";
    }
    std::ifstream in(list);
    for (std::string rel; std::getline(in, rel);) {
        if (rel.empty()) {
            continue;
        }
        const std::string path = std::string(root) + "/" + rel;
        MasterScore* s = compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr);
        s->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(muse::io::path_t(path)));
        const std::filesystem::path out = std::filesystem::path(outDir) / (rel + ".chords");
        std::filesystem::create_directories(out.parent_path());
        std::ofstream o(out);
        if (!compat::loadMsczOrMscx(s, muse::io::path_t(path), false)) {
            o << "NOT READ\n";
            delete s;
            continue;
        }
        s->doLayout();
        for (Measure* m = s->firstMeasure(); m; m = m->nextMeasure()) {
            if (std::getenv("JUBELLE_REFERENCE_DEBUG") && m->system() && m->system()->firstMeasure() == m) {
                std::cerr << rel << ": a system starts at " << m->tick().ticks() << "\n";
            }
            for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                for (track_idx_t t = 0; t < s->ntracks(); ++t) {
                    EngravingItem* e = seg->element(t);
                    if (!e || !e->isChord()) {
                        continue;
                    }
                    Chord* c = toChord(e);
                    for (Chord* g : c->graceNotesBefore()) {
                        chordLayout(o, g, "G");
                    }
                    chordLayout(o, c, "C");
                    for (Chord* g : c->graceNotesAfter()) {
                        chordLayout(o, g, "A");
                    }
                }
            }
        }
        delete s;
    }
}
