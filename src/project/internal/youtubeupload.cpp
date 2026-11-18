/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "youtubeupload.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#elif defined(__APPLE__)
#include <Security/Security.h>
#else
#include <sys/stat.h>
#endif

namespace mu::project::jubelle {
const char* privacyName(Privacy p)
{
    switch (p) {
    case Privacy::Public: return "public";
    case Privacy::Unlisted: return "unlisted";
    case Privacy::Private: break;
    }
    return "private";
}

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    return out;
}

namespace {
//! cut UTF-8 text to at most `maxChars` characters without splitting a character
std::string utf8Prefix(const std::string& s, size_t maxChars, size_t maxBytes)
{
    size_t chars = 0, i = 0;
    while (i < s.size() && chars < maxChars) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + len > maxBytes) {
            break;
        }
        i += len;
        ++chars;
    }
    return s.substr(0, i);
}
}

std::string cleanTitle(const std::string& title)
{
    std::string t;
    for (char c : title) {
        if (c != '<' && c != '>' && c != '\n' && c != '\r') {
            t += c;
        }
    }
    const size_t a = t.find_first_not_of(" \t");
    const size_t b = t.find_last_not_of(" \t");
    t = a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
    t = utf8Prefix(t, 100, std::string::npos);
    return t.empty() ? std::string("Untitled score") : t;
}

std::string uploadMetadata(const std::string& title, const std::string& description, Privacy privacy)
{
    std::string desc;
    for (char c : description) {
        if (c != '<' && c != '>') {   // YouTube rejects angle brackets in descriptions
            desc += c;
        }
    }
    desc = utf8Prefix(desc, std::string::npos, 5000);
    std::ostringstream o;
    o << "{\"snippet\":{\"title\":\"" << jsonEscape(cleanTitle(title)) << "\",\"description\":\"" << jsonEscape(desc)
      << "\",\"categoryId\":\"10\"},\"status\":{\"privacyStatus\":\"" << privacyName(privacy)
      << "\",\"selfDeclaredMadeForKids\":false}}";
    return o.str();
}

std::string videoLink(const std::string& videoId)
{
    return "https://youtu.be/" + videoId;
}

// ---------------------------------------------------------------------------

#ifdef _WIN32
namespace {
std::wstring wide(const std::string& s)
{
    if (s.empty()) {
        return std::wstring();
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::wstring target(const std::string& name)
{
    return wide("Jubelle/" + name);
}
}

bool storeSecret(const std::string& name, const std::string& secret, const std::string&)
{
    std::wstring t = target(name);
    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = t.data();
    cred.CredentialBlobSize = DWORD(secret.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    std::wstring user = L"Jubelle";
    cred.UserName = user.data();
    return CredWriteW(&cred, 0) == TRUE;
}

std::string readSecret(const std::string& name, const std::string&)
{
    PCREDENTIALW cred = nullptr;
    if (CredReadW(target(name).c_str(), CRED_TYPE_GENERIC, 0, &cred) != TRUE || !cred) {
        return std::string();
    }
    std::string out(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
    CredFree(cred);
    return out;
}

void removeSecret(const std::string& name, const std::string&)
{
    CredDeleteW(target(name).c_str(), CRED_TYPE_GENERIC, 0);
}
#elif defined(__APPLE__)
// Jubelle (macOS): secrets go in the login Keychain, under the service "Jubelle", like
// Credential Manager on Windows. The fallback folder isn't used.
namespace {
CFStringRef cfString(const std::string& s)
{
    return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(s.data()), CFIndex(s.size()),
                                   kCFStringEncodingUTF8, false);
}

CFMutableDictionaryRef query(const std::string& name)
{
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFStringRef service = cfString("Jubelle");
    CFStringRef account = cfString(name);
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, service);
    CFDictionarySetValue(q, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return q;
}
}

bool storeSecret(const std::string& name, const std::string& secret, const std::string&)
{
    CFMutableDictionaryRef q = query(name);
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(secret.data()), CFIndex(secret.size()));
    OSStatus st = SecItemCopyMatching(q, nullptr);
    if (st == errSecSuccess) {
        CFMutableDictionaryRef update = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                  &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(update, kSecValueData, data);
        st = SecItemUpdate(q, update);
        CFRelease(update);
    } else {
        CFDictionarySetValue(q, kSecValueData, data);
        st = SecItemAdd(q, nullptr);
    }
    CFRelease(data);
    CFRelease(q);
    return st == errSecSuccess;
}

std::string readSecret(const std::string& name, const std::string&)
{
    CFMutableDictionaryRef q = query(name);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    std::string out;
    if (SecItemCopyMatching(q, &result) == errSecSuccess && result) {
        CFDataRef data = static_cast<CFDataRef>(result);
        out.assign(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), size_t(CFDataGetLength(data)));
        CFRelease(result);
    }
    CFRelease(q);
    return out;
}

void removeSecret(const std::string& name, const std::string&)
{
    CFMutableDictionaryRef q = query(name);
    SecItemDelete(q);
    CFRelease(q);
}
#else
namespace {
std::string secretPath(const std::string& name, const std::string& dir)
{
    return dir + "/jubelle_" + name + ".secret";
}
}

bool storeSecret(const std::string& name, const std::string& secret, const std::string& dir)
{
    if (dir.empty()) {
        return false;
    }
    const std::string path = secretPath(name, dir);
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) {
            return false;
        }
        f << secret;
    }
    chmod(path.c_str(), S_IRUSR | S_IWUSR);   // readable by you only
    return true;
}

std::string readSecret(const std::string& name, const std::string& dir)
{
    if (dir.empty()) {
        return std::string();
    }
    std::ifstream f(secretPath(name, dir), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void removeSecret(const std::string& name, const std::string& dir)
{
    if (!dir.empty()) {
        std::remove(secretPath(name, dir).c_str());
    }
}
#endif
}
