/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * Exact musical time: a fraction of a whole note, always reduced, denominator > 0.
 * Same meaning as MuseScore's Fraction (so files and the bridge convert exactly),
 * with ticks at 480 per quarter note for indexing. Overflow is checked: an operation
 * that cannot be represented throws std::overflow_error rather than being wrong.
 */
#pragma once

#include <cstdint>
#include <string>

namespace je {
class Fraction
{
public:
    static constexpr int TICKS_PER_QUARTER = 480;

    constexpr Fraction() = default;
    Fraction(int64_t numerator, int64_t denominator);

    static Fraction fromTicks(int64_t ticks);

    int64_t numerator() const { return m_num; }
    int64_t denominator() const { return m_den; }

    //! exact when the value is a multiple of 1/1920 (ticks); otherwise rounded to nearest
    int64_t ticks() const;

    Fraction operator+(const Fraction& o) const;
    Fraction operator-(const Fraction& o) const;
    Fraction operator*(const Fraction& o) const;
    Fraction operator/(const Fraction& o) const;
    Fraction operator-() const { return Fraction(-m_num, m_den); }
    Fraction& operator+=(const Fraction& o) { return *this = *this + o; }
    Fraction& operator-=(const Fraction& o) { return *this = *this - o; }

    bool operator==(const Fraction& o) const { return m_num == o.m_num && m_den == o.m_den; }
    bool operator!=(const Fraction& o) const { return !(*this == o); }
    bool operator<(const Fraction& o) const;
    bool operator<=(const Fraction& o) const { return !(o < *this); }
    bool operator>(const Fraction& o) const { return o < *this; }
    bool operator>=(const Fraction& o) const { return !(*this < o); }

    bool isZero() const { return m_num == 0; }
    std::string toString() const;   // "3/8"

private:
    int64_t m_num = 0;
    int64_t m_den = 1;
};
}
