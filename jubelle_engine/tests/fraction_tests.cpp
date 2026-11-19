// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <stdexcept>

#include "model/fraction.h"

using je::Fraction;

TEST(JE_Fraction, ReducedAndSigned)
{
    EXPECT_EQ(Fraction(2, 8), Fraction(1, 4));
    EXPECT_EQ(Fraction(3, -6), Fraction(-1, 2));
    EXPECT_EQ(Fraction(0, 5).denominator(), 1);
    EXPECT_THROW(Fraction(1, 0), std::invalid_argument);
}

TEST(JE_Fraction, Arithmetic)
{
    EXPECT_EQ(Fraction(1, 4) + Fraction(1, 8), Fraction(3, 8));
    EXPECT_EQ(Fraction(1, 4) - Fraction(1, 3), Fraction(-1, 12));
    EXPECT_EQ(Fraction(2, 3) * Fraction(3, 8), Fraction(1, 4));
    EXPECT_EQ(Fraction(1, 4) / Fraction(1, 12), Fraction(3, 1));
    EXPECT_THROW(Fraction(1, 4) / Fraction(0, 1), std::domain_error);
    // a triplet eighth three times is a quarter, exactly
    Fraction t;
    for (int i = 0; i < 3; ++i) {
        t += Fraction(1, 12);
    }
    EXPECT_EQ(t, Fraction(1, 4));
}

TEST(JE_Fraction, Ticks)
{
    EXPECT_EQ(Fraction(1, 4).ticks(), 480);
    EXPECT_EQ(Fraction(1, 1).ticks(), 1920);
    EXPECT_EQ(Fraction::fromTicks(720), Fraction(3, 8));
    EXPECT_EQ(Fraction(1, 12).ticks(), 160);
    EXPECT_EQ(Fraction(1, 7).ticks(), 274);   // not tick-aligned: rounded to nearest
}

TEST(JE_Fraction, Ordering)
{
    EXPECT_LT(Fraction(1, 3), Fraction(1, 2));
    EXPECT_GT(Fraction(-1, 3), Fraction(-1, 2));
    EXPECT_LE(Fraction(2, 4), Fraction(1, 2));
    // very large denominators still compare correctly
    EXPECT_LT(Fraction(1, 4000000000LL), Fraction(1, 3999999999LL));
}

TEST(JE_Fraction, OverflowIsReported)
{
    const Fraction big(INT64_MAX / 2, 1);
    EXPECT_THROW(big * Fraction(4, 1), std::overflow_error);
}
