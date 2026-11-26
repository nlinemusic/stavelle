/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 */
#include "zip.h"

#include <cstdio>
#include <cstring>
#include <fstream>

#include <zlib.h>

#ifdef _WIN32
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace je::io::zip {
namespace {
uint16_t u16(const std::string& b, size_t at) { return uint16_t((unsigned char)b[at] | ((unsigned char)b[at + 1] << 8)); }
uint32_t u32(const std::string& b, size_t at) { return uint32_t(u16(b, at)) | (uint32_t(u16(b, at + 2)) << 16); }
void put16(std::string& o, uint16_t v) { o += char(v & 0xFF); o += char(v >> 8); }
void put32(std::string& o, uint32_t v) { put16(o, uint16_t(v & 0xFFFF)); put16(o, uint16_t(v >> 16)); }

bool inflateRaw(const std::string& in, size_t at, size_t size, size_t expected, std::string& out)
{
    out.assign(expected, '\0');
    z_stream z {};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
        return false;
    }
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data() + at));
    z.avail_in = uInt(size);
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = uInt(expected);
    const int r = inflate(&z, Z_FINISH);
    const bool ok = r == Z_STREAM_END && z.total_out == expected;
    inflateEnd(&z);
    return ok;
}

std::string deflateRaw(const std::string& in)
{
    z_stream z {};
    deflateInit2(&z, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::string out(deflateBound(&z, uLong(in.size())), '\0');
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    z.avail_in = uInt(in.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = uInt(out.size());
    deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return out;
}
}

bool read(const std::string& b, std::vector<Entry>& entries, std::string& error)
{
    entries.clear();
    // the end-of-central-directory record: 22 bytes plus a comment of up to 64 kB, at the end
    if (b.size() < 22) {
        error = "too small to be a ZIP archive";
        return false;
    }
    size_t eocd = std::string::npos;
    for (size_t i = b.size() - 22 + 1; i-- > 0 && b.size() - i <= 22 + 0xFFFF;) {
        if (u32(b, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        error = "not a ZIP archive (no central directory)";
        return false;
    }
    const uint16_t count = u16(b, eocd + 10);
    size_t at = u32(b, eocd + 16);
    for (uint16_t i = 0; i < count; ++i) {
        if (at + 46 > b.size() || u32(b, at) != 0x02014b50) {
            error = "damaged central directory";
            return false;
        }
        const uint16_t flags = u16(b, at + 8);
        const uint16_t method = u16(b, at + 10);
        Entry e;
        e.dosTime = u16(b, at + 12);
        e.dosDate = u16(b, at + 14);
        const uint32_t crc = u32(b, at + 16);
        const uint32_t csize = u32(b, at + 20);
        const uint32_t usize = u32(b, at + 24);
        const uint16_t nlen = u16(b, at + 28), xlen = u16(b, at + 30), clen = u16(b, at + 32);
        const uint32_t local = u32(b, at + 42);
        if (at + 46 + nlen > b.size()) {
            error = "damaged central directory";
            return false;
        }
        e.name = b.substr(at + 46, nlen);
        at += 46 + nlen + xlen + clen;
        if (flags & 1) {
            error = "encrypted entries aren't supported (" + e.name + ")";
            return false;
        }
        if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF) {
            error = "ZIP64 isn't supported (" + e.name + ")";
            return false;
        }
        if (size_t(local) + 30 > b.size() || u32(b, local) != 0x04034b50) {
            error = "damaged entry " + e.name;
            return false;
        }
        const size_t data = local + 30 + u16(b, local + 26) + u16(b, local + 28);
        if (data + csize > b.size()) {
            error = "truncated entry " + e.name;
            return false;
        }
        if (method == 0) {
            e.data = b.substr(data, csize);
            e.stored = true;
        } else if (method == 8) {
            if (!inflateRaw(b, data, csize, usize, e.data)) {
                error = "can't decompress " + e.name;
                return false;
            }
        } else {
            error = "unsupported compression in " + e.name;
            return false;
        }
        if (uint32_t(crc32(0, reinterpret_cast<const Bytef*>(e.data.data()), uInt(e.data.size()))) != crc) {
            error = "checksum mismatch in " + e.name;
            return false;
        }
        entries.push_back(std::move(e));
    }
    return true;
}

std::string write(const std::vector<Entry>& entries)
{
    std::string out;
    std::string central;
    for (const Entry& e : entries) {
        const uint32_t crc = uint32_t(crc32(0, reinterpret_cast<const Bytef*>(e.data.data()), uInt(e.data.size())));
        std::string packed = e.stored ? e.data : deflateRaw(e.data);
        bool stored = e.stored;
        if (!stored && packed.size() >= e.data.size()) {
            packed = e.data;
            stored = true;
        }
        const uint32_t local = uint32_t(out.size());
        auto header = [&](std::string& o, bool isCentral) {
            put32(o, isCentral ? 0x02014b50 : 0x04034b50);
            if (isCentral) {
                put16(o, 20);   // made by
            }
            put16(o, 20);       // version needed
            put16(o, 0x0800);   // UTF-8 names
            put16(o, stored ? 0 : 8);
            put16(o, e.dosTime);
            put16(o, e.dosDate);
            put32(o, crc);
            put32(o, uint32_t(packed.size()));
            put32(o, uint32_t(e.data.size()));
            put16(o, uint16_t(e.name.size()));
            put16(o, 0);        // extra
            if (isCentral) {
                put16(o, 0);    // comment
                put16(o, 0);    // disk
                put16(o, 0);    // internal attributes
                put32(o, 0);    // external attributes
                put32(o, local);
            }
            o += e.name;
        };
        header(out, false);
        out += packed;
        header(central, true);
    }
    const uint32_t cdStart = uint32_t(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, uint16_t(entries.size()));
    put16(out, uint16_t(entries.size()));
    put32(out, uint32_t(central.size()));
    put32(out, cdStart);
    put16(out, 0);
    return out;
}

bool readFile(const std::string& path, std::string& bytes)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(const std::string& path, const std::string& bytes)
{
    // The new bytes go to a temporary file beside the score and are put on the disk before it replaces the score in
    // one step, so a crash or a full disk during saving leaves the old file whole (and the journal still matches it)
    const std::string tmp = path + ".jubelle-saving";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        return false;
    }
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size() && std::fflush(f) == 0;
#ifdef _WIN32
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;
#endif
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        std::remove(tmp.c_str());
        return false;
    }
#ifdef _WIN32
    if (MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    std::remove(tmp.c_str());
    return false;
#else
    if (std::rename(tmp.c_str(), path.c_str()) == 0) {
        return true;
    }
    std::remove(tmp.c_str());
    return false;
#endif
}
}
