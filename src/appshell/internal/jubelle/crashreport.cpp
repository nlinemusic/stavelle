/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). See crashreport.h.
 */
#include "crashreport.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define JUBELLE_HAVE_BACKTRACE 1
#endif
#endif

namespace mu::appshell::jubelle {
// ---------------------------------------------------------------------------
// The report as text
// ---------------------------------------------------------------------------

std::string CrashReport::toText() const
{
    std::ostringstream out;
    out << "jubelle-crash 1\n";
    out << "version " << version << '\n';
    out << "os " << os << '\n';
    out << "kind " << kind << '\n';
    out << "when " << when << '\n';
    for (const std::string& f : frames) {
        out << "frame " << f << '\n';
    }
    if (!logTail.empty()) {
        out << "log\n" << logTail;
        if (logTail.back() != '\n') {
            out << '\n';
        }
    }
    return out.str();
}

CrashReport CrashReport::fromText(const std::string& text)
{
    CrashReport r;
    std::istringstream in(text);
    std::string line;
    bool header = false, inLog = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!header) {
            if (line.rfind("jubelle-crash", 0) != 0) {
                return CrashReport();
            }
            header = true;
            continue;
        }
        if (inLog) {
            r.logTail += line + '\n';
            continue;
        }
        auto value = [&](const char* key) -> const char* {
            const size_t n = std::strlen(key);
            return line.compare(0, n, key) == 0 && line.size() > n && line[n] == ' ' ? line.c_str() + n + 1 : nullptr;
        };
        if (const char* v = value("version")) {
            r.version = v;
        } else if (const char* v2 = value("os")) {
            r.os = v2;
        } else if (const char* v3 = value("kind")) {
            r.kind = v3;
        } else if (const char* v4 = value("when")) {
            r.when = v4;
        } else if (const char* v5 = value("frame")) {
            r.frames.push_back(v5);
        } else if (line == "log") {
            inLog = true;
        }
    }
    return r;
}

std::string CrashReport::signature() const
{
    // FNV-1a over the kind and the top five frames
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        h ^= 0xff;
        h *= 1099511628211ULL;
    };
    mix(kind);
    for (size_t i = 0; i < frames.size() && i < 5; ++i) {
        mix(frames[i]);
    }
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", unsigned(h ^ (h >> 32)));
    return buf;
}

std::string CrashReport::issueTitle() const
{
    std::string title = "[crash " + signature() + "] " + (kind.empty() ? std::string("Crash") : kind);
    if (!frames.empty()) {
        title += " at " + frames.front();
    }
    if (!version.empty()) {
        title += " (" + version + ")";
    }
    return title;
}

std::string CrashReport::issueBody(const std::string& homeDir, const std::string& userName) const
{
    std::ostringstream out;
    out << "**Jubelle** " << version << "  \n";
    out << "**System** " << os << "  \n";
    out << "**What happened** " << kind << "  \n";
    out << "**When** " << when << "  \n";
    out << "**Signature** `" << signature() << "`\n\n";
    if (!frames.empty()) {
        out << "Where (module + offset, top first):\n```\n";
        for (size_t i = 0; i < frames.size() && i < 32; ++i) {
            out << frames[i] << '\n';
        }
        out << "```\n";
    }
    if (!logTail.empty()) {
        out << "\nLast log lines (cleaned):\n```\n" << logTail << "```\n";
    }
    out << "\n<!-- Write here what you were doing, if you remember. -->\n";
    return sanitize(out.str(), homeDir, userName);
}

// ---------------------------------------------------------------------------
// Cleaning
// ---------------------------------------------------------------------------

static std::string replaceInsensitive(const std::string& text, const std::string& what, const std::string& with)
{
    if (what.empty()) {
        return text;
    }
    std::string lowerText = text, lowerWhat = what;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::transform(lowerWhat.begin(), lowerWhat.end(), lowerWhat.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::string out;
    size_t pos = 0;
    while (true) {
        const size_t found = lowerText.find(lowerWhat, pos);
        if (found == std::string::npos) {
            out += text.substr(pos);
            break;
        }
        out += text.substr(pos, found - pos) + with;
        pos = found + what.size();
    }
    return out;
}

std::string sanitize(const std::string& input, const std::string& homeDir, const std::string& userName)
{
    std::string s = input;

    // 1. the home folder, written either way
    if (homeDir.size() > 2) {
        std::string fwd = homeDir, back = homeDir;
        std::replace(fwd.begin(), fwd.end(), '\\', '/');
        std::replace(back.begin(), back.end(), '/', '\\');
        s = replaceInsensitive(s, fwd, "[home]");
        s = replaceInsensitive(s, back, "[home]");
    }

    // 2. every path and file name (folders and files are often score titles)
    // Folder names can contain spaces, so a path runs to the end of its line,
    // or to a quote, ';' or ',' (more is removed rather than too little)
    static const std::regex windowsPath(R"([A-Za-z]:[\\/][^\n"'<>|*?;,]*)");
    static const std::regex uncPath(R"(\\\\[^\n"'<>|*?;,]+)");
    static const std::regex homePath(R"(\[home\][\\/][^\n"'<>|*?;,]*)");
    static const std::regex posixPath(R"((^|[\s"'(=:])(/[^\s"'<>/]+(/[^\n"'<>;,]*)+))");
    static const std::regex fileName(
        R"([^\s"'<>|*?\\/]+\.(mscz|mscx|mscs|mscbackup|mxl|musicxml|xml|mid|midi|kar|cap|capx|gp|gpx|gp5|ptb|ove|bww|pdf|png|svg|wav|mp3|flac|ogg|mss|mpal|drm|json|txt|log)\b)",
        std::regex::icase);
    s = std::regex_replace(s, homePath, "[path]");
    s = std::regex_replace(s, windowsPath, "[path]");
    s = std::regex_replace(s, uncPath, "[path]");
    s = std::regex_replace(s, posixPath, "$1[path]");
    s = std::regex_replace(s, fileName, "[file]");

    // 3. e-mail addresses
    static const std::regex email(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})");
    s = std::regex_replace(s, email, "[email]");

    // 4. keys and tokens
    static const std::regex ghToken(R"(\b(gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})\b)");
    static const std::regex bearer(R"((Bearer|token|Authorization:?)\s+[A-Za-z0-9._~+/=-]{12,})", std::regex::icase);
    static const std::regex longHex(R"(\b[A-Fa-f0-9]{32,}\b)");
    static const std::regex longOpaque(R"([A-Za-z0-9+/_=-]{40,})");
    s = std::regex_replace(s, ghToken, "[token]");
    s = std::regex_replace(s, bearer, "$1 [token]");
    s = std::regex_replace(s, longHex, "[token]");
    s = std::regex_replace(s, longOpaque, "[token]");

    // 5. the user name itself, anywhere else it appears
    if (userName.size() >= 3) {
        s = replaceInsensitive(s, userName, "[user]");
    }
    return s;
}

// ---------------------------------------------------------------------------
// Links
// ---------------------------------------------------------------------------

std::string urlEncode(const std::string& text)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string newIssueUrl(const std::string& repository, const std::string& title, const std::string& body)
{
    // Browsers and GitHub accept about 8000 characters in a link: shorten the body if needed
    const std::string base = "https://github.com/" + repository + "/issues/new?labels=crash&title=" + urlEncode(title) + "&body=";
    const size_t limit = 7800;
    std::string b = body;
    std::string encoded = urlEncode(b);
    while (base.size() + encoded.size() > limit && b.size() > 200) {
        b = b.substr(0, b.size() * 3 / 4) + "\n…(shortened)\n```\n";
        encoded = urlEncode(b);
    }
    return base + encoded;
}

std::string searchIssuesUrl(const std::string& repository, const std::string& signature)
{
    return "https://github.com/" + repository + "/issues?q=" + urlEncode("is:issue \"crash " + signature + "\"");
}

// ---------------------------------------------------------------------------
// Small files
// ---------------------------------------------------------------------------

std::set<std::string> readSentSignatures(const std::string& file)
{
    std::set<std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            out.insert(line);
        }
    }
    return out;
}

void rememberSentSignature(const std::string& file, const std::string& signature)
{
    if (readSentSignatures(file).count(signature)) {
        return;
    }
    std::ofstream out(file, std::ios::app);
    out << signature << '\n';
}

void markSessionOpen(const std::string& markerFile, const std::string& version)
{
    std::ofstream out(markerFile, std::ios::trunc);
    out << version << '\n';
}

void markSessionClosed(const std::string& markerFile)
{
    std::remove(markerFile.c_str());
}

bool sessionWasLeftOpen(const std::string& markerFile, std::string* version)
{
    std::ifstream in(markerFile);
    if (!in) {
        return false;
    }
    std::string v;
    std::getline(in, v);
    if (version) {
        *version = v;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The crash handler. It runs while the program is failing, so it only formats
// into fixed buffers and writes one file; nothing is cleaned or sent here.
// ---------------------------------------------------------------------------

namespace {
char g_reportPath[2048] = { 0 };
char g_version[128] = { 0 };
char g_os[256] = { 0 };

void copyFixed(char* dst, size_t size, const std::string& src)
{
    std::snprintf(dst, size, "%s", src.c_str());
}

#ifdef _WIN32
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

const char* exceptionName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case 0xE06D7363: return "C++ exception (not caught)";
    case 0xC0000409: return "STACK_BUFFER_OVERRUN (fast fail)";
    default: return nullptr;
    }
}

// "module.dll+0x1234" for an address (the module's file name only, never its folder)
void describeAddress(void* address, char* out, size_t size)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module) && module) {
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* name = path;
        for (const char* p = path; *p; ++p) {
            if (*p == '\\' || *p == '/') {
                name = p + 1;
            }
        }
        std::snprintf(out, size, "%s+0x%llx", name,
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
    } else {
        std::snprintf(out, size, "?+0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)));
    }
}

LONG WINAPI jubelleCrashFilter(EXCEPTION_POINTERS* info)
{
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0 && g_reportPath[0]) {
        static char text[16384];
        int n = 0;
        const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
        const char* name = exceptionName(code);
        SYSTEMTIME t;
        GetLocalTime(&t);
        n += std::snprintf(text + n, sizeof(text) - n, "jubelle-crash 1\nversion %s\nos %s\n", g_version, g_os);
        if (name) {
            n += std::snprintf(text + n, sizeof(text) - n, "kind %s\n", name);
        } else {
            n += std::snprintf(text + n, sizeof(text) - n, "kind exception 0x%08lx\n", static_cast<unsigned long>(code));
        }
        n += std::snprintf(text + n, sizeof(text) - n, "when %04d-%02d-%02d %02d:%02d:%02d\n", t.wYear, t.wMonth, t.wDay,
                           t.wHour, t.wMinute, t.wSecond);
        char frame[512];
        if (info && info->ExceptionRecord) {
            describeAddress(info->ExceptionRecord->ExceptionAddress, frame, sizeof(frame));
            n += std::snprintf(text + n, sizeof(text) - n, "frame %s\n", frame);
        }
        void* stack[48];
        const USHORT count = RtlCaptureStackBackTrace(0, 48, stack, nullptr);
        for (USHORT i = 0; i < count && n < int(sizeof(text)) - 600; ++i) {
            describeAddress(stack[i], frame, sizeof(frame));
            n += std::snprintf(text + n, sizeof(text) - n, "frame %s\n", frame);
        }
        HANDLE file = CreateFileA(g_reportPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, text, DWORD(n), &written, nullptr);
            CloseHandle(file);
        }
    }
    return g_previousFilter ? g_previousFilter(info) : EXCEPTION_CONTINUE_SEARCH;
}
#else
struct sigaction g_previous[32];

void jubelleSignalHandler(int sig)
{
    static volatile sig_atomic_t once = 0;
    if (!once && g_reportPath[0]) {
        once = 1;
        char text[16384];
        int n = std::snprintf(text, sizeof(text), "jubelle-crash 1\nversion %s\nos %s\nkind signal %d (%s)\n", g_version, g_os, sig,
                              sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGFPE ? "SIGFPE"
                              : sig == SIGILL ? "SIGILL" : sig == SIGBUS ? "SIGBUS" : "?");
#ifdef JUBELLE_HAVE_BACKTRACE
        void* stack[48];
        const int count = backtrace(stack, 48);
        for (int i = 0; i < count && n < int(sizeof(text)) - 600; ++i) {
            Dl_info dl;
            if (dladdr(stack[i], &dl) && dl.dli_fname) {
                const char* name = dl.dli_fname;
                for (const char* p = dl.dli_fname; *p; ++p) {
                    if (*p == '/') {
                        name = p + 1;
                    }
                }
                n += std::snprintf(text + n, sizeof(text) - n, "frame %s+0x%llx\n", name,
                                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(stack[i])
                                                                   - reinterpret_cast<uintptr_t>(dl.dli_fbase)));
            }
        }
#endif
        const int fd = open(g_reportPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) {
            ssize_t ignored = write(fd, text, size_t(n));
            (void)ignored;
            close(fd);
        }
    }
    // then whatever handled it before
    if (sig > 0 && sig < 32) {
        sigaction(sig, &g_previous[sig], nullptr);
    }
    raise(sig);
}
#endif
}

void installCrashHandler(const std::string& reportFile, const std::string& version, const std::string& os)
{
    copyFixed(g_reportPath, sizeof(g_reportPath), reportFile);
    copyFixed(g_version, sizeof(g_version), version);
    copyFixed(g_os, sizeof(g_os), os);
#ifdef _WIN32
    g_previousFilter = SetUnhandledExceptionFilter(jubelleCrashFilter);
#else
    for (int sig : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS }) {
        struct sigaction action;
        std::memset(&action, 0, sizeof(action));
        action.sa_handler = jubelleSignalHandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESETHAND;
        sigaction(sig, &action, &g_previous[sig]);
    }
#endif
}
}
