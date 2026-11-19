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
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engraving/compat/mscxcompat.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/articulation.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/infrastructure/localfileinfoprovider.h"

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
                out << "# annotation " << a->typeName() << " " << frac(seg->tick()) << " " << a->track() << "\n";
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
            std::ofstream(out) << listing(s);
            ++done;
        }
        delete s;
    }
    std::cout << "reference listings: " << done << " read, " << failed << " not read\n";
}
