/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "importexport/videoexport/internal/jubelle/audiosplice.h"

using namespace mu::iex::videoexport::jubelle;

namespace {
std::string tempPath(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

// A stereo test signal whose value tells you where in the file you are:
// left = frame / 1000, right = -left
std::vector<float> ramp(int frames)
{
    std::vector<float> s(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        s[size_t(i) * 2] = float(i) / 1000.0f;
        s[size_t(i) * 2 + 1] = -float(i) / 1000.0f;
    }
    return s;
}

std::vector<float> run(WavReader& w, const std::vector<Piece>& pieces, double fadeSec = 0.0)
{
    std::vector<float> out;
    SpliceOptions o;
    o.crossfadeSec = fadeSec;
    o.blockFrames = 100;   // small blocks so joins fall inside and across blocks
    splice(w, pieces, o, [&out](int64_t n, const float* s) { out.insert(out.end(), s, s + n * 2); });
    return out;
}

void writeIntWav(const std::string& path, int bits, const std::vector<int32_t>& values)
{
    std::ofstream f(path, std::ios::binary);
    auto put = [&f](uint32_t v, int n) { for (int i = 0; i < n; ++i) { char c = char((v >> (8 * i)) & 0xFF); f.write(&c, 1); } };
    const int bytes = bits / 8;
    const uint32_t data = uint32_t(values.size() * size_t(bytes));
    f.write("RIFF", 4); put(36 + data, 4); f.write("WAVEfmt ", 8); put(16, 4);
    put(1, 2); put(1, 2); put(1000, 4); put(1000 * uint32_t(bytes), 4); put(uint32_t(bytes), 2); put(uint32_t(bits), 2);
    f.write("LIST", 4); put(4, 4); f.write("INFO", 4);   // an extra chunk to skip
    f.write("data", 4); put(data, 4);
    for (int32_t v : values) {
        put(uint32_t(v), bytes);
    }
}
}

TEST(Jubelle_AudioSplice, ReadsFloatWav)
{
    const std::string p = tempPath("jubelle_splice_float.wav");
    ASSERT_TRUE(writeFloatWav(p, 1000, 2, ramp(500)));
    WavReader w;
    ASSERT_TRUE(w.open(p)) << w.error();
    EXPECT_EQ(w.sampleRate(), 1000);
    EXPECT_EQ(w.channels(), 2);
    EXPECT_EQ(w.frames(), 500);
    EXPECT_TRUE(w.isFloat());
    std::vector<float> buf;
    w.read(498, 4, buf);   // past the end reads as silence
    ASSERT_EQ(buf.size(), 8u);
    EXPECT_FLOAT_EQ(buf[0], 0.498f);
    EXPECT_FLOAT_EQ(buf[2], 0.499f);
    EXPECT_FLOAT_EQ(buf[4], 0.0f);
    std::remove(p.c_str());
}

TEST(Jubelle_AudioSplice, ReadsIntegerWavs)
{
    for (int bits : { 16, 24, 32 }) {
        const std::string p = tempPath("jubelle_splice_int.wav");
        const int32_t full = bits == 16 ? 32767 : bits == 24 ? 8388607 : 2147483647;
        writeIntWav(p, bits, { 0, full / 2, -full / 2, full });
        WavReader w;
        ASSERT_TRUE(w.open(p)) << bits << " " << w.error();
        EXPECT_EQ(w.bitsPerSample(), bits);
        EXPECT_EQ(w.frames(), 4);
        std::vector<float> buf;
        w.read(0, 4, buf);
        EXPECT_NEAR(buf[0], 0.0f, 1e-6);
        EXPECT_NEAR(buf[1], 0.5f, 1e-4);
        EXPECT_NEAR(buf[2], -0.5f, 1e-4);
        EXPECT_NEAR(buf[3], 1.0f, 1e-4);
        std::remove(p.c_str());
    }
}

TEST(Jubelle_AudioSplice, NoPiecesPassesEverythingThrough)
{
    const std::string p = tempPath("jubelle_splice_all.wav");
    const std::vector<float> src = ramp(1234);
    ASSERT_TRUE(writeFloatWav(p, 1000, 2, src));
    WavReader w;
    ASSERT_TRUE(w.open(p));
    EXPECT_EQ(run(w, {}), src);
    std::remove(p.c_str());
}

TEST(Jubelle_AudioSplice, PiecesInOrderWithRepeatKeepExactLength)
{
    const std::string p = tempPath("jubelle_splice_pieces.wav");
    ASSERT_TRUE(writeFloatWav(p, 1000, 2, ramp(1000)));
    WavReader w;
    ASSERT_TRUE(w.open(p));
    // lead-in 0-0.1 s, then 0.5-0.7 s, then a repeat of 0.1-0.3 s
    const std::vector<Piece> pieces { { 0.0, 0.1 }, { 0.5, 0.7 }, { 0.1, 0.3 } };
    EXPECT_EQ(splicedFrames(w, pieces), 500);

    const std::vector<float> hard = run(w, pieces, 0.0);
    ASSERT_EQ(hard.size(), 1000u);
    EXPECT_FLOAT_EQ(hard[99 * 2], 0.099f);
    EXPECT_FLOAT_EQ(hard[100 * 2], 0.5f);     // hard cut to 0.5 s
    EXPECT_FLOAT_EQ(hard[300 * 2], 0.1f);     // and back to 0.1 s

    const std::vector<float> faded = run(w, pieces, 0.010);   // 10 frames
    ASSERT_EQ(faded.size(), 1000u);   // the length never changes
    // Inside the first join the old continuation (0.100...) fades out as the new piece (0.5...) fades in
    const double t = 0.5 / 10.0;
    EXPECT_NEAR(faded[100 * 2], 0.5 * std::sin(1.5707963 * t) + 0.100 * std::cos(1.5707963 * t), 1e-5);
    EXPECT_NEAR(faded[100 * 2 + 1], -faded[100 * 2], 1e-6);   // channels stay separate
    // After the fade it is the new piece alone
    EXPECT_FLOAT_EQ(faded[110 * 2], 0.51f);
    // Equal power: the gains' squares add to 1 across the whole fade
    for (int k = 0; k < 10; ++k) {
        const double tt = (k + 0.5) / 10.0;
        EXPECT_NEAR(std::pow(std::sin(1.5707963 * tt), 2) + std::pow(std::cos(1.5707963 * tt), 2), 1.0, 1e-6);
    }
    // A piece that simply continues the previous one is not faded
    const std::vector<float> cont = run(w, { { 0.0, 0.2 }, { 0.2, 0.4 } }, 0.010);
    EXPECT_FLOAT_EQ(cont[200 * 2], 0.2f);
    std::remove(p.c_str());
}

TEST(Jubelle_AudioSplice, ShortPieceAndRejectsBadFiles)
{
    const std::string p = tempPath("jubelle_splice_short.wav");
    ASSERT_TRUE(writeFloatWav(p, 1000, 2, ramp(100)));
    WavReader w;
    ASSERT_TRUE(w.open(p));
    // A 3-frame piece after a jump: the fade is limited to the piece
    const std::vector<float> out = run(w, { { 0.0, 0.01 }, { 0.05, 0.053 } }, 0.020);
    EXPECT_EQ(out.size(), 13u * 2);
    std::remove(p.c_str());

    const std::string bad = tempPath("jubelle_splice_bad.wav");
    { std::ofstream f(bad, std::ios::binary); f << "not a wav file at all"; }
    WavReader b;
    EXPECT_FALSE(b.open(bad));
    EXPECT_FALSE(b.error().empty());
    std::remove(bad.c_str());
}
