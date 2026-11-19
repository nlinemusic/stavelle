/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Report a Bug: automatic details.
 */
#include "bugreportdiagnostics.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <QDir>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QSysInfo>

#include "global/jubelle/diagnosticsregistry.h"
#include "../crashguard.h"
#include "../performanceprofile.h"

namespace fs = std::filesystem;
using muse::jubelle::DiagnosticSection;
using muse::jubelle::DiagnosticsRegistry;

namespace mu::appshell::jubelle::bugreport {
namespace {
std::string graphicsApiName()
{
    switch (QQuickWindow::graphicsApi()) {
    case QSGRendererInterface::OpenGL: return "OpenGL";
    case QSGRendererInterface::Direct3D11: return "Direct3D 11";
    case QSGRendererInterface::Direct3D12: return "Direct3D 12";
    case QSGRendererInterface::Vulkan: return "Vulkan";
    case QSGRendererInterface::Metal: return "Metal";
    case QSGRendererInterface::Software: return "Software";
    default: return "other";
    }
}
}

void registerApplicationDiagnostics(const std::string& version, const std::string& build, const std::string& revision)
{
    DiagnosticsRegistry::instance().add("application", [version, build, revision]() {
        DiagnosticSection s;
        s.id = "application";
        s.title = "Jubelle";
        s.order = 10;
        s.items.push_back({ "Version", version + (build.empty() ? "" : "." + build) });
        if (!revision.empty()) {
            s.items.push_back({ "Revision", revision });
        }
        s.items.push_back({ "Qt", qVersion() });
        return s;
    });

    DiagnosticsRegistry::instance().add("system", []() {
        DiagnosticSection s;
        s.id = "system";
        s.title = "System";
        s.order = 20;
        s.items.push_back({ "Operating system", QSysInfo::prettyProductName().toStdString() });
        s.items.push_back({ "Kernel", (QSysInfo::kernelType() + " " + QSysInfo::kernelVersion()).toStdString() });
        s.items.push_back({ "Architecture", QSysInfo::currentCpuArchitecture().toStdString() });
        s.items.push_back({ "Graphics", graphicsApiName() });
        const auto screens = QGuiApplication::screens();
        s.items.push_back({ "Screens", std::to_string(screens.size()) });
        for (int i = 0; i < screens.size(); ++i) {
            const QScreen* sc = screens.at(i);
            std::ostringstream v;
            v << sc->size().width() << "x" << sc->size().height() << " at " << sc->devicePixelRatio() << "x";
            s.items.push_back({ "Screen " + std::to_string(i + 1), v.str() });
        }
        return s;
    });

    DiagnosticsRegistry::instance().add("hardware", []() {
        DiagnosticSection s;
        s.id = "hardware";
        s.title = "Hardware";
        s.order = 25;
        const auto m = mu::appshell::jubelle::currentMachine();
        std::ostringstream mem;
        mem.precision(1);
        mem << std::fixed << m.memoryGb << " GB";
        s.items.push_back({ "Processor cores", std::to_string(m.cores) });
        s.items.push_back({ "Memory", mem.str() });
        return s;
    });
}

RawDiagnostics gatherDiagnostics(const std::string& appDataDir)
{
    RawDiagnostics raw;
    raw.sections = DiagnosticsRegistry::instance().collectAll();
    raw.logTail = readNewestLogTail((fs::u8path(appDataDir) / "logs").u8string());

    std::error_code ec;
    const fs::path crash = fs::u8path(CrashGuard::folder(appDataDir)) / "last-crash.txt";
    if (fs::exists(crash, ec)) {
        std::ifstream in(crash, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        raw.lastCrash = ss.str();
    }
    return raw;
}

PrivacyContext currentPrivacy()
{
    PrivacyContext p;
    p.homeDir = QDir::toNativeSeparators(QDir::homePath()).toStdString();
    p.userName = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER")).toStdString();
    return p;
}

std::string reportsFolder(const std::string& appDataDir)
{
    return (fs::u8path(appDataDir) / "bug-reports").u8string();
}

std::string nowIsoUtc()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm {};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}
}
