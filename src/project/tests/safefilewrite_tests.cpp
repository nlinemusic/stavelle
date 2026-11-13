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
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "project/internal/safefilewrite.h"

namespace fs = std::filesystem;
using namespace mu::project::safefilewrite;

namespace {
std::string utf8(const fs::path& p)
{
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

std::vector<uint8_t> readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> filled(size_t size, uint8_t value)
{
    return std::vector<uint8_t>(size, value);
}

bool isUniform(const std::vector<uint8_t>& data, uint8_t value)
{
    for (uint8_t b : data) {
        if (b != value) {
            return false;
        }
    }
    return true;
}

//! Mirrors the order of operations in NotationProject::doSave.
bool safeSave(const fs::path& target, const std::vector<uint8_t>& data)
{
    fs::path temp = target;
    temp += "_saving";
    if (!writeFileDurably(utf8(temp), data.data(), data.size())) {
        return false;
    }
    if (readAll(temp) != data) {
        fs::remove(temp);
        return false;
    }
    return replaceFileAtomically(utf8(temp), utf8(target));
}
}

class Project_SafeFileWriteTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::random_device rd;
        m_dir = fs::temp_directory_path() / ("stavelle_safesave_" + std::to_string(rd()));
        fs::create_directories(m_dir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    fs::path m_dir;
};

TEST_F(Project_SafeFileWriteTests, WriteDurably_WritesExactBytes)
{
    const fs::path file = m_dir / "score.mscz";
    std::vector<uint8_t> data;
    for (int i = 0; i < 100000; ++i) {
        data.push_back(static_cast<uint8_t>(i * 31));
    }

    std::string err;
    ASSERT_TRUE(writeFileDurably(utf8(file), data.data(), data.size(), &err)) << err;
    EXPECT_EQ(readAll(file), data);
}

TEST_F(Project_SafeFileWriteTests, WriteDurably_TruncatesLongerExistingFile)
{
    const fs::path file = m_dir / "score.mscz";
    // Note: "small" is a macro in Windows headers, so avoid it as a name.
    const auto longData = filled(5000, 'x');
    const auto shortData = filled(10, 'y');

    ASSERT_TRUE(writeFileDurably(utf8(file), longData.data(), longData.size()));
    ASSERT_TRUE(writeFileDurably(utf8(file), shortData.data(), shortData.size()));
    EXPECT_EQ(readAll(file), shortData);
}

TEST_F(Project_SafeFileWriteTests, WriteDurably_EmptyData)
{
    const fs::path file = m_dir / "empty.bin";
    ASSERT_TRUE(writeFileDurably(utf8(file), nullptr, 0));
    EXPECT_TRUE(fs::exists(file));
    EXPECT_EQ(fs::file_size(file), 0u);
}

TEST_F(Project_SafeFileWriteTests, WriteDurably_FailsForMissingFolder)
{
    const fs::path file = m_dir / "does-not-exist" / "score.mscz";
    const auto data = filled(10, 'a');
    std::string err;
    EXPECT_FALSE(writeFileDurably(utf8(file), data.data(), data.size(), &err));
    EXPECT_FALSE(err.empty());
}

TEST_F(Project_SafeFileWriteTests, Replace_ReplacesExistingAndConsumesSource)
{
    const fs::path target = m_dir / "score.mscz";
    const fs::path source = m_dir / "score.mscz_saving";
    const auto oldData = filled(3000, 'o');
    const auto newData = filled(1000, 'n');

    ASSERT_TRUE(writeFileDurably(utf8(target), oldData.data(), oldData.size()));
    ASSERT_TRUE(writeFileDurably(utf8(source), newData.data(), newData.size()));

    std::string err;
    ASSERT_TRUE(replaceFileAtomically(utf8(source), utf8(target), &err)) << err;
    EXPECT_EQ(readAll(target), newData);
    EXPECT_FALSE(fs::exists(source));
}

TEST_F(Project_SafeFileWriteTests, Replace_CreatesTargetWhenMissing)
{
    const fs::path target = m_dir / "new score.mscz";
    const fs::path source = m_dir / "new score.mscz_saving";
    const auto data = filled(100, 'n');

    ASSERT_TRUE(writeFileDurably(utf8(source), data.data(), data.size()));
    ASSERT_TRUE(replaceFileAtomically(utf8(source), utf8(target)));
    EXPECT_EQ(readAll(target), data);
}

TEST_F(Project_SafeFileWriteTests, Replace_FailureLeavesTargetUntouched)
{
    const fs::path target = m_dir / "score.mscz";
    const auto oldData = filled(3000, 'o');
    ASSERT_TRUE(writeFileDurably(utf8(target), oldData.data(), oldData.size()));

    std::string err;
    EXPECT_FALSE(replaceFileAtomically(utf8(m_dir / "missing_saving"), utf8(target), &err));
    EXPECT_FALSE(err.empty());
    EXPECT_EQ(readAll(target), oldData);
}

TEST_F(Project_SafeFileWriteTests, UnicodeAndSpacesInPaths)
{
    const fs::path folder = m_dir / fs::u8path(u8"Big Band - Jazz Band");
    fs::create_directories(folder);
    const fs::path target = folder / fs::u8path(u8"Café Ré♭ – Ballade.mscz");
    const auto v1 = filled(500, 1);
    const auto v2 = filled(700, 2);

    ASSERT_TRUE(safeSave(target, v1));
    ASSERT_TRUE(safeSave(target, v2));
    EXPECT_EQ(readAll(target), v2);
}

TEST_F(Project_SafeFileWriteTests, LargeScoreFile)
{
    const fs::path target = m_dir / "orchestra.mscz";
    std::vector<uint8_t> data(64 * 1024 * 1024);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i ^ (i >> 8));
    }

    ASSERT_TRUE(safeSave(target, data));
    EXPECT_EQ(readAll(target), data);
}

//! While the score is saved over and over, a concurrent reader must only ever
//! see one complete version or the other, never a truncated or mixed file.
TEST_F(Project_SafeFileWriteTests, ConcurrentReaderNeverSeesPartialFile)
{
    const fs::path target = m_dir / "score.mscz";
    const auto versionA = filled(512 * 1024, 'A');
    const auto versionB = filled(1024 * 1024, 'B');
    ASSERT_TRUE(safeSave(target, versionA));

    std::atomic<bool> stop { false };
    std::atomic<int> reads { 0 };
    std::atomic<int> badReads { 0 };

    std::thread reader([&]() {
        while (!stop) {
            std::ifstream in(target, std::ios::binary);
            if (!in) {
                continue; // Windows: briefly unopenable during the swap; never partial.
            }
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const bool ok = (data.size() == versionA.size() && isUniform(data, 'A'))
                            || (data.size() == versionB.size() && isUniform(data, 'B'));
            if (!ok) {
                ++badReads;
            }
            ++reads;
#ifdef _WIN32
            // Windows cannot swap a file while another program holds it open, so give
            // the writer realistic gaps (as a real reader, e.g. a sync client, would).
            in.close();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        }
    });

    for (int i = 0; i < 150; ++i) {
        ASSERT_TRUE(safeSave(target, (i % 2) ? versionA : versionB)) << "save " << i;
    }

    stop = true;
    reader.join();

    EXPECT_GT(reads.load(), 0);
    EXPECT_EQ(badReads.load(), 0);
}

//! Helper run in a separate process by the Windows crash test below: saves the
//! file named in STAVELLE_SAVE_LOOP_TARGET forever, until it is killed.
TEST(Project_SafeFileWriteChild, DISABLED_SaveLoop)
{
    const char* target = std::getenv("STAVELLE_SAVE_LOOP_TARGET");
    if (!target) {
        return;
    }
    const fs::path path = fs::u8path(target);
    const auto versionA = filled(2 * 1024 * 1024, 'A');
    const auto versionB = filled(3 * 1024 * 1024, 'B');
    for (int i = 0;; ++i) {
        safeSave(path, (i % 2) ? versionA : versionB);
    }
}

#ifdef _WIN32
//! Simulates the application crashing at random moments during saving: a child
//! process saves continuously and is terminated without warning (like "End task").
//! After every termination the score on disk must be a complete version.
TEST_F(Project_SafeFileWriteTests, CrashDuringSaveNeverCorruptsScore)
{
    const fs::path target = m_dir / "score.mscz";
    const auto versionA = filled(2 * 1024 * 1024, 'A');
    const auto versionB = filled(3 * 1024 * 1024, 'B');
    ASSERT_TRUE(safeSave(target, versionA));

    wchar_t self[MAX_PATH * 4] = {};
    ASSERT_GT(GetModuleFileNameW(nullptr, self, MAX_PATH * 4), 0u);
    ASSERT_TRUE(SetEnvironmentVariableW(L"STAVELLE_SAVE_LOOP_TARGET", target.wstring().c_str()));

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> delayMs(50, 400);

    for (int round = 0; round < 25; ++round) {
        std::wstring cmd = L"\"" + std::wstring(self) + L"\" --gtest_also_run_disabled_tests "
                                                        L"--gtest_filter=Project_SafeFileWriteChild.DISABLED_SaveLoop";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        ASSERT_TRUE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));

        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs(rng)));
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        const auto data = readAll(target);
        const bool ok = (data.size() == versionA.size() && isUniform(data, 'A'))
                        || (data.size() == versionB.size() && isUniform(data, 'B'));
        ASSERT_TRUE(ok) << "score corrupted after simulated crash in round " << round << " (size " << data.size() << ")";
    }

    SetEnvironmentVariableW(L"STAVELLE_SAVE_LOOP_TARGET", nullptr);
}

#else
//! Simulates the application crashing at random moments during saving: a child
//! process saves continuously and is killed without warning. After every kill
//! the score on disk must be a complete version.
TEST_F(Project_SafeFileWriteTests, CrashDuringSaveNeverCorruptsScore)
{
    const fs::path target = m_dir / "score.mscz";
    const auto versionA = filled(2 * 1024 * 1024, 'A');
    const auto versionB = filled(3 * 1024 * 1024, 'B');
    ASSERT_TRUE(safeSave(target, versionA));

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> delayMs(1, 60);

    for (int round = 0; round < 25; ++round) {
        pid_t pid = fork();
        ASSERT_GE(pid, 0);
        if (pid == 0) {
            for (int i = 0;; ++i) {
                safeSave(target, (i % 2) ? versionA : versionB);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs(rng)));
        kill(pid, SIGKILL);
        int status = 0;
        waitpid(pid, &status, 0);

        const auto data = readAll(target);
        const bool ok = (data.size() == versionA.size() && isUniform(data, 'A'))
                        || (data.size() == versionB.size() && isUniform(data, 'B'));
        ASSERT_TRUE(ok) << "score corrupted after simulated crash in round " << round << " (size " << data.size() << ")";
    }
}

#endif
