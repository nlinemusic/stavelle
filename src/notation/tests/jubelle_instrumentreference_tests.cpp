/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G10: Learning Center instrument reference, from the real instrument data.
 */
#include <gtest/gtest.h>

#include "engraving/dom/instrtemplate.h"
#include "notation/internal/jubelle/instrumentreference.h"

#include "templatebuilder.h"

using namespace mu::engraving;
namespace jr = mu::notation::jubelle;

namespace {
jr::InstrumentReference ref(const char16_t* id)
{
    jubelle_templates::loadInstrumentData();
    jr::InstrumentReference r;
    const InstrumentIndex idx = searchTemplateIndexForId(muse::String(id));
    EXPECT_TRUE(idx.instrTemplate) << "no instrument " << muse::String(id).toStdString();
    if (idx.instrTemplate) {
        EXPECT_TRUE(jr::referenceFor(*idx.instrTemplate, &r));
    }
    return r;
}
}

TEST(Jubelle_InstrumentReference, PitchNames)
{
    EXPECT_EQ(jr::pitchName(60).toStdString(), "C4");
    EXPECT_EQ(jr::pitchName(58).toStdString(), "B♭3");
    EXPECT_EQ(jr::pitchName(66, true).toStdString(), "F♯4");
    EXPECT_EQ(jr::pitchName(21).toStdString(), "A0");
    EXPECT_EQ(jr::pitchName(108).toStdString(), "C8");
}

TEST(Jubelle_InstrumentReference, TranspositionsInWords)
{
    EXPECT_EQ(jr::transpositionText(0).toStdString(), "Concert pitch (sounds as written)");
    EXPECT_EQ(jr::transpositionText(-2).toStdString(), "Sounds a major 2nd lower (written C sounds B♭)");
    EXPECT_EQ(jr::transpositionText(-9).toStdString(), "Sounds a major 6th lower (written C sounds E♭)");
    EXPECT_EQ(jr::transpositionText(-14).toStdString(), "Sounds an octave and a major 2nd lower (written C sounds B♭)");
    EXPECT_EQ(jr::transpositionText(-7).toStdString(), "Sounds a perfect 5th lower (written C sounds F)");
    EXPECT_EQ(jr::transpositionText(12).toStdString(), "Sounds an octave higher");
    EXPECT_EQ(jr::transpositionText(-12).toStdString(), "Sounds an octave lower");
}

TEST(Jubelle_InstrumentReference, TheBigBandHornsAgreeWithTheInstrumentData)
{
    const jr::InstrumentReference trumpet = ref(u"bb-trumpet");
    EXPECT_TRUE(trumpet.transposing);
    EXPECT_EQ(trumpet.transposition.toStdString(), "Sounds a major 2nd lower (written C sounds B♭)");
    EXPECT_FALSE(trumpet.writtenRange.isEmpty());
    EXPECT_NE(trumpet.writtenRange, trumpet.soundingRange) << "a transposing instrument is written at a different pitch";

    const jr::InstrumentReference alto = ref(u"alto-saxophone");
    EXPECT_EQ(alto.transposition.toStdString(), "Sounds a major 6th lower (written C sounds E♭)");

    const jr::InstrumentReference tenor = ref(u"tenor-saxophone");
    EXPECT_EQ(tenor.transposition.toStdString(), "Sounds an octave and a major 2nd lower (written C sounds B♭)");

    const jr::InstrumentReference trombone = ref(u"trombone");
    EXPECT_FALSE(trombone.transposing);
    EXPECT_EQ(trombone.writtenRange, trombone.soundingRange);

    const jr::InstrumentReference bass = ref(u"contrabass");
    EXPECT_EQ(bass.transposition.toStdString(), "Sounds an octave lower");
}

TEST(Jubelle_InstrumentReference, DrumKitsHaveNoRange)
{
    jubelle_templates::loadInstrumentData();
    const InstrumentIndex idx = searchTemplateIndexForId(u"drumset");
    ASSERT_TRUE(idx.instrTemplate);
    jr::InstrumentReference r;
    EXPECT_FALSE(jr::referenceFor(*idx.instrTemplate, &r));
}
