/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 */
#include "fraction.h"

#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace je {
namespace {
int64_t checkedMul(int64_t a, int64_t b)
{
#if defined(__GNUC__) || defined(__clang__)
    int64_t r = 0;
    if (__builtin_mul_overflow(a, b, &r)) {
        throw std::overflow_error("je::Fraction: overflow");
    }
    return r;
#else
    if (a != 0 && (std::abs(b) > std::numeric_limits<int64_t>::max() / std::abs(a))) {
        throw std::overflow_error("je::Fraction: overflow");
    }
    return a * b;
#endif
}

int64_t checkedAdd(int64_t a, int64_t b)
{
    if ((b > 0 && a > std::numeric_limits<int64_t>::max() - b) || (b < 0 && a < std::numeric_limits<int64_t>::min() - b)) {
        throw std::overflow_error("je::Fraction: overflow");
    }
    return a + b;
}
}

Fraction::Fraction(int64_t numerator, int64_t denominator)
{
    if (denominator == 0) {
        throw std::invalid_argument("je::Fraction: zero denominator");
    }
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    const int64_t g = std::gcd(numerator < 0 ? -numerator : numerator, denominator);
    m_num = g ? numerator / g : 0;
    m_den = g ? denominator / g : 1;
    if (m_num == 0) {
        m_den = 1;
    }
}

Fraction Fraction::fromTicks(int64_t ticks)
{
    return Fraction(ticks, TICKS_PER_QUARTER * 4);
}

int64_t Fraction::ticks() const
{
    // ticks = num * 1920 / den, rounded to nearest (exact for tick-aligned values)
    const int64_t scaled = checkedMul(m_num, TICKS_PER_QUARTER * 4);
    const int64_t q = scaled / m_den;
    const int64_t r = scaled % m_den;
    if (2 * std::abs(r) >= m_den) {
        return q + (scaled < 0 ? -1 : 1);
    }
    return q;
}

Fraction Fraction::operator+(const Fraction& o) const
{
    const int64_t g = std::gcd(m_den, o.m_den);
    const int64_t den = checkedMul(m_den / g, o.m_den);
    return Fraction(checkedAdd(checkedMul(m_num, o.m_den / g), checkedMul(o.m_num, m_den / g)), den);
}

Fraction Fraction::operator-(const Fraction& o) const
{
    return *this + (-o);
}

Fraction Fraction::operator*(const Fraction& o) const
{
    // cross-reduce first to keep numbers small
    const int64_t g1 = std::gcd(m_num < 0 ? -m_num : m_num, o.m_den);
    const int64_t g2 = std::gcd(o.m_num < 0 ? -o.m_num : o.m_num, m_den);
    const int64_t a = g1 ? m_num / g1 : m_num;
    const int64_t d = g1 ? o.m_den / g1 : o.m_den;
    const int64_t c = g2 ? o.m_num / g2 : o.m_num;
    const int64_t b = g2 ? m_den / g2 : m_den;
    return Fraction(checkedMul(a, c), checkedMul(b, d));
}

Fraction Fraction::operator/(const Fraction& o) const
{
    if (o.m_num == 0) {
        throw std::domain_error("je::Fraction: division by zero");
    }
    return *this * Fraction(o.m_den, o.m_num);
}

bool Fraction::operator<(const Fraction& o) const
{
    // compare a/b < c/d without overflow: use long double only as a fallback
    const int64_t g = std::gcd(m_den, o.m_den);
    try {
        return checkedMul(m_num, o.m_den / g) < checkedMul(o.m_num, m_den / g);
    } catch (const std::overflow_error&) {
        return (long double)m_num / (long double)m_den < (long double)o.m_num / (long double)o.m_den;
    }
}

std::string Fraction::toString() const
{
    return std::to_string(m_num) + "/" + std::to_string(m_den);
}
}
