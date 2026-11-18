/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "audiosplice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mu::iex::videoexport::jubelle {
namespace {
uint32_t le32(const unsigned char* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint16_t le16(const unsigned char* p) { return uint16_t(p[0] | (p[1] << 8)); }

constexpr uint16_t FORMAT_PCM = 1;
constexpr uint16_t FORMAT_FLOAT = 3;
constexpr uint16_t FORMAT_EXTENSIBLE = 0xFFFE;
constexpr double HALF_PI = 1.57079632679489661923;

int64_t toFrame(double secs, int rate)
{
    return int64_t(std::llround(std::max(0.0, secs) * rate));
}
}

bool WavReader::open(const std::string& path)
{
    m_file.close();
    m_file.clear();
    m_file.open(path, std::ios::binary);
    if (!m_file) {
        m_error = "cannot open " + path;
        return false;
    }
    unsigned char hdr[12];
    if (!m_file.read(reinterpret_cast<char*>(hdr), 12) || std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        m_error = "not a WAV file";
        return false;
    }
    bool haveFmt = false;
    uint16_t format = 0;
    while (true) {
        unsigned char ch[8];
        if (!m_file.read(reinterpret_cast<char*>(ch), 8)) {
            m_error = "no audio data";
            return false;
        }
        const uint32_t size = le32(ch + 4);
        if (std::memcmp(ch, "fmt ", 4) == 0) {
            std::vector<unsigned char> f(size);
            if (size < 16 || !m_file.read(reinterpret_cast<char*>(f.data()), size)) {
                m_error = "bad format chunk";
                return false;
            }
            format = le16(f.data());
            m_channels = le16(f.data() + 2);
            m_sampleRate = int(le32(f.data() + 4));
            m_bits = le16(f.data() + 14);
            if (format == FORMAT_EXTENSIBLE && size >= 26) {
                format = le16(f.data() + 24);   // first two bytes of the sub-format GUID
            }
            haveFmt = true;
        } else if (std::memcmp(ch, "data", 4) == 0) {
            if (!haveFmt) {
                m_error = "data before format";
                return false;
            }
            m_dataStart = m_file.tellg();
            m_float = format == FORMAT_FLOAT;
            const bool okFormat = (format == FORMAT_PCM && (m_bits == 16 || m_bits == 24 || m_bits == 32))
                                  || (m_float && m_bits == 32);
            if (!okFormat || m_channels <= 0 || m_sampleRate <= 0) {
                m_error = "unsupported WAV format";
                return false;
            }
            const int frameBytes = m_channels * (m_bits / 8);
            // A writer that couldn't seek back may leave the size at 0 or too large: use the file length
            m_file.seekg(0, std::ios::end);
            const std::streamoff avail = m_file.tellg() - m_dataStart;
            const std::streamoff declared = (size == 0 || size == 0xFFFFFFFFu) ? avail : std::min<std::streamoff>(size, avail);
            m_frames = int64_t(declared / frameBytes);
            return true;
        } else {
            m_file.seekg(size + (size & 1), std::ios::cur);
        }
    }
}

void WavReader::read(int64_t frame, int64_t count, std::vector<float>& out)
{
    out.assign(size_t(std::max<int64_t>(0, count) * m_channels), 0.0f);
    if (count <= 0) {
        return;
    }
    const int64_t first = std::max<int64_t>(frame, 0);
    const int64_t last = std::min<int64_t>(frame + count, m_frames);
    if (first >= last) {
        return;
    }
    const int bytes = m_bits / 8;
    const int64_t n = last - first;
    m_raw.resize(size_t(n * m_channels * bytes));
    m_file.clear();
    m_file.seekg(m_dataStart + std::streamoff(first * m_channels * bytes));
    m_file.read(m_raw.data(), std::streamsize(m_raw.size()));
    const int64_t got = int64_t(m_file.gcount()) / (m_channels * bytes);

    float* dst = out.data() + size_t((first - frame) * m_channels);
    const unsigned char* src = reinterpret_cast<const unsigned char*>(m_raw.data());
    const int64_t values = got * m_channels;
    for (int64_t i = 0; i < values; ++i, src += bytes) {
        float v = 0.0f;
        if (m_float) {
            uint32_t u = le32(src);
            std::memcpy(&v, &u, 4);
        } else if (bytes == 2) {
            v = float(int16_t(le16(src))) / 32768.0f;
        } else if (bytes == 3) {
            int32_t s = int32_t(uint32_t(src[0]) << 8 | uint32_t(src[1]) << 16 | uint32_t(src[2]) << 24) >> 8;
            v = float(s) / 8388608.0f;
        } else {
            v = float(double(int32_t(le32(src))) / 2147483648.0);
        }
        dst[i] = v;
    }
}

int64_t splicedFrames(const WavReader& wav, const std::vector<Piece>& pieces)
{
    if (pieces.empty()) {
        return wav.frames();
    }
    int64_t total = 0;
    for (const Piece& p : pieces) {
        total += std::max<int64_t>(0, toFrame(p.second, wav.sampleRate()) - toFrame(p.first, wav.sampleRate()));
    }
    return total;
}

int64_t splice(WavReader& wav, const std::vector<Piece>& pieces, const SpliceOptions& options,
               const std::function<void(int64_t frames, const float* samples)>& sink)
{
    const int rate = wav.sampleRate();
    const int ch = wav.channels();
    const int64_t block = std::max<int64_t>(64, options.blockFrames);
    std::vector<float> buf;
    std::vector<float> tail;

    std::vector<std::pair<int64_t, int64_t> > ranges;
    if (pieces.empty()) {
        ranges.push_back({ 0, wav.frames() });
    } else {
        for (const Piece& p : pieces) {
            const int64_t a = toFrame(p.first, rate);
            const int64_t b = toFrame(p.second, rate);
            if (b > a) {
                ranges.push_back({ a, b });
            }
        }
    }

    int64_t produced = 0;
    int64_t prevEnd = -1;
    for (const auto& [start, end] : ranges) {
        // A join that doesn't simply continue gets a crossfade over the first
        // frames of the new piece, mixed with what would have followed the old one
        int64_t fade = 0;
        if (prevEnd >= 0 && start != prevEnd) {
            fade = std::min<int64_t>(toFrame(options.crossfadeSec, rate), end - start);
            if (fade > 0) {
                wav.read(prevEnd, fade, tail);
            }
        }
        for (int64_t pos = start; pos < end; pos += block) {
            const int64_t n = std::min(block, end - pos);
            wav.read(pos, n, buf);
            const int64_t offset = pos - start;
            for (int64_t k = offset; k < std::min<int64_t>(fade, offset + n); ++k) {
                const double t = (double(k) + 0.5) / double(fade);
                const float fin = float(std::sin(HALF_PI * t));
                const float fout = float(std::cos(HALF_PI * t));
                for (int c = 0; c < ch; ++c) {
                    float& v = buf[size_t((k - offset) * ch + c)];
                    v = v * fin + tail[size_t(k * ch + c)] * fout;
                }
            }
            sink(n, buf.data());
            produced += n;
        }
        prevEnd = end;
    }
    return produced;
}

bool writeFloatWav(const std::string& path, int sampleRate, int channels, const std::vector<float>& samples)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    auto put32 = [&f](uint32_t v) { unsigned char b[4] = { uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24) };
                                    f.write(reinterpret_cast<char*>(b), 4); };
    auto put16 = [&f](uint16_t v) { unsigned char b[2] = { uint8_t(v), uint8_t(v >> 8) };
                                    f.write(reinterpret_cast<char*>(b), 2); };
    const uint32_t dataBytes = uint32_t(samples.size() * 4);
    f.write("RIFF", 4);
    put32(36 + dataBytes);
    f.write("WAVEfmt ", 8);
    put32(16);
    put16(FORMAT_FLOAT);
    put16(uint16_t(channels));
    put32(uint32_t(sampleRate));
    put32(uint32_t(sampleRate * channels * 4));
    put16(uint16_t(channels * 4));
    put16(32);
    f.write("data", 4);
    put32(dataBytes);
    for (float v : samples) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        put32(u);
    }
    return bool(f);
}
}
