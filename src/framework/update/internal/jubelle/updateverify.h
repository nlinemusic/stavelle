/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * R3 (decision D6, 15 Nov 2026): updates come only from Jubelle's GitHub
 * Releases, and an installer is used only if
 *  1. the release's SHA256SUMS file carries a valid Ed25519 signature made
 *     with Jubelle's release key (whose public half is built into the app), and
 *  2. the installer's SHA-256 matches its line in that file.
 * Anything else stops the update and the download is deleted.
 *
 * Plain C++ with no Qt, so it is tested without the app. Ed25519 follows
 * TweetNaCl (D. J. Bernstein et al., public domain); SHA-256 and SHA-512 are
 * FIPS 180-4.
 */
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace muse::update::jubelle {
using Bytes = std::vector<uint8_t>;

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t size);
std::array<uint8_t, 64> sha512(const uint8_t* data, size_t size);
std::string toHex(const uint8_t* data, size_t size);
bool fromHex(const std::string& hex, Bytes& out);

//! Ed25519 signature check (RFC 8032): true only for a valid signature
bool ed25519Verify(const uint8_t signature[64], const uint8_t* message, size_t size, const uint8_t publicKey[32]);

//! "<sha256 hex>  <file name>" lines, as written by sha256sum
std::map<std::string, std::string> parseSha256Sums(const std::string& text);

enum class Verdict {
    Ok,
    NoReleaseKey,        // this build has no release key: it can't verify anything
    BadSignature,        // the checksum list isn't signed by Jubelle's key
    NotListed,           // the installer isn't in the checksum list
    WrongChecksum,       // the installer was changed or damaged
};

//! The whole check. `signature` is the 64-byte signature (raw, or hex text).
Verdict verifyInstaller(const Bytes& installer, const std::string& installerName, const std::string& sha256sums,
                        const Bytes& signature, const std::string& publicKeyHex);

//! The release key built into this build (hex), or "" if none was set yet
std::string releasePublicKeyHex();

//! Jubelle version order: 4.7.7.261116010 > 4.7.7.261115091 (all numeric parts compared)
int compareVersions(const std::string& a, const std::string& b);
}
