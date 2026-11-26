/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine: tests for lyric entry (edit::LyricsEntry). The same keys are compared with MuseScore's own lyric
 * entry on every corpus score (je_measures_reference.py … lyrics, in CI).
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "edit/history.h"
#include "edit/lyrics.h"
#include "edit/operations.h"
#include "edit/scorebuilder.h"
#include "edit/structure.h"
#include "io/mscxreader.h"
#include "io/mscxwriter.h"
#include "model/fields.h"
#include "model/invariants.h"
#include "model/model.h"

using namespace je;
using namespace je::edit;
using je::model::Kind;
namespace F = je::model::fields;

namespace {
struct Doc {
    model::Model model;
    IdGenerator ids { 1 };
    ScoreBuilder b { model, ids };
    Operations ops { model, ids };
    History history;
    ScoreBuilder::PlayerIds voice;
    std::vector<Id> chords;

    //! two measures of quarter notes, or the rhythm given (quarters; 0: a quarter rest)
    explicit Doc(std::vector<int> pitches = { 60, 62, 64, 65, 67, 69, 71, 72 })
    {
        Transaction tx(model, "New score");
        b.newScore(tx, "Lyrics");
        b.addMeasures(tx, int((pitches.size() + 3) / 4), 4, 4);
        voice = b.addPlayer(tx, "Voice", "voice", "voices", 1);
        for (size_t i = 0; i < pitches.size(); ++i) {
            const Fraction t(int64_t(i), 4);
            chords.push_back(pitches[i] ? b.addChord(tx, voice.voice, t, Fraction(1, 4), { pitches[i] })
                                        : b.addRest(tx, voice.voice, t, Fraction(1, 4)));
        }
        history.push(tx.commit());
    }
    //! types the keys as je_io_tool's lyricsScript does, from chord `first`, in one undo step
    void type(size_t first, const std::string& keys)
    {
        Transaction tx(model, "Lyrics");
        LyricsEntry entry(ops);
        ASSERT_TRUE(entry.start(tx, chords[first]));
        std::string typed;
        for (char c : keys) {
            if (std::string("-_ </^!").find(c) == std::string::npos) {
                typed += c;
                continue;
            }
            if (!typed.empty()) {
                entry.setText(typed);
            }
            const Id was = entry.current();
            switch (c) {
            case '-': entry.nextSyllable(tx); break;
            case '_': entry.melisma(tx); break;
            case ' ': entry.nextWord(tx); break;
            case '<': entry.nextWord(tx, true); break;
            case '/': entry.addVerse(tx); break;
            case '^': entry.changeVerse(tx, true); break;
            case '!': entry.changeVerse(tx, false); break;
            default: break;
            }
            if (!entry.editing() || entry.current() != was) {
                typed.clear();
            }
        }
        if (!typed.empty()) {
            entry.setText(typed);
        }
        entry.finish(tx);
        history.push(tx.commit());
    }
    //! each chord's lyrics: "verse:syllabic:extension:text", "." where there are none
    std::string lyrics() const
    {
        std::string out;
        for (Id c : chords) {
            std::string here;
            for (Id l : model.find(c)->children()) {
                const model::Object* o = model.find(l);
                if (o->kind() == Kind::Attachment && o->text(F::SUBKIND) == "lyric") {
                    const std::string syl = o->text(F::SYLLABIC);
                    here += (here.empty() ? "" : "+") + std::to_string(o->intOr(F::VERSE, 0)) + ":" + (syl.empty() ? "s" : syl.substr(0, 1))
                            + ":" + o->fractionOr(F::DURATION, Fraction()).toString() + ":" + o->text(F::TEXT);
                }
            }
            out += (out.empty() ? "" : " ") + (here.empty() ? std::string(".") : here);
        }
        return out;
    }
};
}

TEST(Lyrics, SyllablesAreJoinedByDashesAndWordsBySpaces)
{
    Doc d;
    d.type(0, "Glo-ry to God");
    EXPECT_EQ(d.lyrics(), "0:b:0/1:Glo 0:e:0/1:ry 0:s:0/1:to 0:s:0/1:God . . . .");
    EXPECT_TRUE(model::checkInvariants(d.model.snapshot()).empty());
}

TEST(Lyrics, AnEmptySyllableIsTakenBackWithTheStepThatMadeIt)
{
    Doc d;
    // two dashes: the dash runs on over the second chord to the third
    d.type(0, "a--b");
    EXPECT_EQ(d.lyrics(), "0:b:0/1:a . 0:e:0/1:b . . . . .");
    // two spaces: a word skips a chord
    Doc e;
    e.type(0, "a  b");
    EXPECT_EQ(e.lyrics(), "0:s:0/1:a . 0:s:0/1:b . . . . .");
}

TEST(Lyrics, UnderscoresHoldTheSyllableOverTheNextChords)
{
    Doc d;
    d.type(0, "a___b");
    // the extension reaches the third chord (MuseScore counts it to the start of the last chord held)
    EXPECT_EQ(d.lyrics(), "0:s:1/2:a . . 0:s:0/1:b . . . .");
    Doc one;
    one.type(0, "a_b");   // over one chord: the line ends at the next syllable
    EXPECT_EQ(one.lyrics(), "0:s:1/1920:a 0:s:0/1:b . . . . . .");
}

TEST(Lyrics, ADashChangesAWordIntoSyllablesAndASpaceBack)
{
    Doc d;
    d.type(0, "one two");
    d.type(0, "-");   // a new verse on the first chord: the first verse is left as it was
    EXPECT_EQ(d.lyrics(), "0:s:0/1:one 0:s:0/1:two . . . . . .");
    // editing the first verse again: a dash from "one" makes "two" its last syllable
    Transaction tx(d.model, "Edit");
    LyricsEntry entry(d.ops);
    Id first;
    for (Id l : d.model.find(d.chords[0])->children()) {
        first = d.model.find(l)->text(F::SUBKIND) == "lyric" ? l : first;
    }
    ASSERT_TRUE(entry.edit(first));
    entry.nextSyllable(tx);
    entry.finish(tx);
    d.history.push(tx.commit());
    EXPECT_EQ(d.lyrics(), "0:b:0/1:one 0:e:0/1:two . . . . . .");
}

TEST(Lyrics, EnterAddsAVerseAndTheArrowsMoveBetweenVerses)
{
    Doc d;
    d.type(0, "one/uno^ein!dos");
    EXPECT_EQ(d.lyrics(), "0:s:0/1:ein+1:s:0/1:dos . . . . . . .");
}

TEST(Lyrics, NoDashIntoARest)
{
    Doc d({ 60, 0, 64, 65 });
    d.type(0, "a-b");   // the dash goes to the next chord, past the rest
    EXPECT_EQ(d.lyrics(), "0:b:0/1:a . 0:e:0/1:b .");
}

TEST(Lyrics, EntryIsOneUndoStepAndIsSavedAndReopened)
{
    Doc d;
    const std::string before = d.lyrics();
    d.type(0, "Glo-ry__ to");
    const std::string after = d.lyrics();
    ASSERT_NE(after, before);
    d.history.undo(d.model);
    EXPECT_EQ(d.lyrics(), before);
    d.history.redo(d.model);
    EXPECT_EQ(d.lyrics(), after);

    const auto path = std::filesystem::temp_directory_path() / "je_lyrics_roundtrip.mscz";
    std::string error;
    ASSERT_TRUE(io::saveScore(d.model.snapshot(), path.string(), error)) << error;
    model::Model back;
    IdGenerator backIds { 1 };
    ASSERT_TRUE(io::openScore(path.string(), back, backIds, error)) << error;
    EXPECT_EQ(io::musicListing(back.snapshot(), false, true), io::musicListing(d.model.snapshot(), false, true));
    std::filesystem::remove(path);
}
