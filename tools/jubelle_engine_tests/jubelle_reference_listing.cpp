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
#include "engraving/dom/note.h"
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
                    for (Segment* seg = s->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
                        for (EngravingItem* a : seg->annotations()) {
                            if (a->staffIdx() == staff->idx() && !a->systemFlag()) {
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
            std::ofstream(out) << head << listing(s);
            ++done;
        }
        delete s;
    }
    std::cout << "reference listings: " << done << " read, " << failed << " not read\n";
}
