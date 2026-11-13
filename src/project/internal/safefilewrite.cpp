/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Stavelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "safefilewrite.h"

#include <algorithm>
#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace mu::project::safefilewrite {
static void setError(std::string* errorText, const std::string& text)
{
    if (errorText) {
        *errorText = text;
    }
}

#ifdef _WIN32

static std::wstring toWide(const std::string& utf8)
{
    if (utf8.empty()) {
        return std::wstring();
    }

    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);

    for (wchar_t& ch : wide) {
        if (ch == L'/') {
            ch = L'\\';
        }
    }

    // Allow paths longer than MAX_PATH (deeply nested project folders).
    const bool isAbsoluteDrivePath = wide.size() > 2 && wide[1] == L':';
    if (isAbsoluteDrivePath && wide.size() >= 240 && wide.rfind(L"\\\\?\\", 0) != 0) {
        wide = L"\\\\?\\" + wide;
    }

    return wide;
}

static std::string lastErrorText(const char* what)
{
    DWORD code = GetLastError();
    return std::string(what) + " (Windows error " + std::to_string(code) + ")";
}

bool writeFileDurably(const std::string& path, const uint8_t* data, size_t size, std::string* errorText)
{
    HANDLE file = CreateFileW(toWide(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        setError(errorText, lastErrorText("Could not create file"));
        return false;
    }

    size_t written = 0;
    while (written < size) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - written, 1u << 30));
        DWORD n = 0;
        if (!WriteFile(file, data + written, chunk, &n, nullptr) || n == 0) {
            setError(errorText, lastErrorText("Could not write file (is the disk full?)"));
            CloseHandle(file);
            return false;
        }
        written += n;
    }

    if (!FlushFileBuffers(file)) {
        setError(errorText, lastErrorText("Could not flush file to disk"));
        CloseHandle(file);
        return false;
    }

    if (!CloseHandle(file)) {
        setError(errorText, lastErrorText("Could not close file"));
        return false;
    }

    return true;
}

bool replaceFileAtomically(const std::string& sourcePath, const std::string& targetPath, std::string* errorText)
{
    const std::wstring src = toWide(sourcePath);
    const std::wstring dst = toWide(targetPath);

    // Antivirus scanners and cloud-sync clients (OneDrive, Dropbox, iCloud)
    // often hold a score open briefly right after it changes. Retry for up to
    // about two seconds before giving up; the target stays intact meanwhile.
    constexpr int MAX_ATTEMPTS = 20;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        if (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }

        const DWORD code = GetLastError();
        const bool transient = code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION;
        if (!transient || attempt == MAX_ATTEMPTS) {
            setError(errorText, "Could not replace the score file (Windows error " + std::to_string(code)
                     + "). The previously saved version is unchanged.");
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return false;
}

#else // POSIX (macOS, Linux)

static std::string errnoText(const char* what)
{
    return std::string(what) + " (" + std::strerror(errno) + ")";
}

static bool flushToStorage(int fd)
{
#ifdef __APPLE__
    // On macOS, fsync() only reaches the drive's cache; F_FULLFSYNC asks the
    // drive to commit to permanent storage. Some filesystems (e.g. network
    // shares) don't support it, in which case fall back to fsync().
    if (fcntl(fd, F_FULLFSYNC) == 0) {
        return true;
    }
#endif
    while (fsync(fd) != 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

static std::string parentDir(const std::string& path)
{
    const size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

bool writeFileDurably(const std::string& path, const uint8_t* data, size_t size, std::string* errorText)
{
    int fd = -1;
    do {
        fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        setError(errorText, errnoText("Could not create file"));
        return false;
    }

    size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(fd, data + written, size - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            setError(errorText, errnoText("Could not write file"));
            ::close(fd);
            return false;
        }
        if (n == 0) {
            setError(errorText, "Could not write file (is the disk full?)");
            ::close(fd);
            return false;
        }
        written += static_cast<size_t>(n);
    }

    if (!flushToStorage(fd)) {
        setError(errorText, errnoText("Could not flush file to disk"));
        ::close(fd);
        return false;
    }

    if (::close(fd) != 0) {
        setError(errorText, errnoText("Could not close file"));
        return false;
    }

    return true;
}

bool replaceFileAtomically(const std::string& sourcePath, const std::string& targetPath, std::string* errorText)
{
    // rename() atomically replaces the target on POSIX filesystems.
    if (::rename(sourcePath.c_str(), targetPath.c_str()) != 0) {
        setError(errorText, errnoText("Could not replace the score file") + ". The previously saved version is unchanged.");
        return false;
    }

    // Make the rename itself durable (best effort; not all filesystems allow this).
    int dirFd = ::open(parentDir(targetPath).c_str(), O_RDONLY | O_CLOEXEC);
    if (dirFd >= 0) {
        flushToStorage(dirFd);
        ::close(dirFd);
    }

    return true;
}

#endif
}
