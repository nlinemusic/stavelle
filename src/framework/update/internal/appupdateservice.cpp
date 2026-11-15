/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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

#include "appupdateservice.h"

#include <QBuffer>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QJsonDocument>

#include "update/updateerrors.h"
#include "jubelle/updateverify.h"

#include "defer.h"
#include "translation.h"
#include "log.h"

using namespace muse;
using namespace muse::update;
using namespace muse::network;
using namespace muse::async;
using namespace muse::io;

const QString INSTALLED_WEEK_BEGINNING_KEY("Installed-Week-Beginning");
const QString PREVIOUS_REQUEST_DAY_KEY("Previous-Request-Day");

static QDate calculateWeekBeginForDate(const QDate& date)
{
    // 1 (Monday) + 6 mod 7 = 0
    // 2 (Tuesday) + 6 mod 7 = 1
    // etc...
    int diffToWeekBegin = (date.dayOfWeek() + 6) % 7;
    return QDate::currentDate().addDays(-diffToWeekBegin);
}

static ISystemInfo::CpuArchitecture assetArch(const QString& asset)
{
    if (asset.contains("aarch64")) {
        return ISystemInfo::CpuArchitecture::Arm64;
    } else if (asset.contains("arm")) {
        return ISystemInfo::CpuArchitecture::Arm;
    }

    return ISystemInfo::CpuArchitecture::x86_64;
}

static PrevReleasesNotesList parsePreviousReleasesNotes(const QByteArray& json)
{
    PrevReleasesNotesList result;

    QJsonParseError err;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        LOGE() << "failed parse, err: " << err.errorString();
        return result;
    }

    QJsonObject jsonObject = jsonDoc.object();

    if (jsonObject.empty()) {
        LOGE() << "failed parse, no jsonObject";
        return result;
    }

    QJsonArray releases = jsonObject.value("releases").toArray();
    for (const QJsonValue& release : releases) {
        QJsonObject releaseObj = release.toObject();
        std::string version = releaseObj.value("version").toString().toStdString();

        result.push_back({ std::move(version), releaseObj.value("notes").toString().toStdString() });
    }

    return result;
}

void AppUpdateService::init()
{
    TRACEFUNC;

    io::path_t historyPath = configuration()->updateRequestHistoryJsonPath();
    if (fileSystem()->exists(historyPath)) {
        return;
    }

    // If the request history file doesn't exist, perform a first time setup using today's date...
    UpdateRequestHistory updateRequestHistory;
    updateRequestHistory.installedWeekBeginning = calculateWeekBeginForDate(QDate::currentDate());
    updateRequestHistory.previousRequestDay = QDate::currentDate();

    Ret ret = writeUpdateRequestHistory(historyPath, updateRequestHistory);
    if (!ret) {
        LOGE() << ret.toString();
    }
}

Promise<RetVal<ReleaseInfo> > AppUpdateService::checkForUpdate()
{
    return Promise<RetVal<ReleaseInfo> >([this](auto resolve, auto) {
        clear();

        if (configuration()->checkForUpdateTestMode()) {
            return resolve(m_lastCheckResult);
        }

        if (!m_networkManager) {
            m_networkManager = networkManagerCreator()->makeNetworkManager();
        }

        // Read request history file...
        io::path_t historyPath = configuration()->updateRequestHistoryJsonPath();
        RetVal<UpdateRequestHistory> historyRv = readUpdateRequestHistory(historyPath);
        if (!historyRv.ret) {
            LOGE() << historyRv.ret.toString();
        }

        const std::string urlText = configuration()->checkForAppUpdateUrl();
        m_jubelleFeed = isJubelleReleasesUrl(urlText);   // Jubelle R3: asked once, kept for the download
        QUrl url = QString::fromStdString(urlText);
        RequestHeaders headers = prepareHeaders(historyRv.val);
        auto buff = std::make_shared<QBuffer>();

        RetVal<Progress> progress = m_networkManager->get(url, buff, headers);
        if (!progress.ret) {
            m_lastCheckResult.ret = progress.ret;
            return resolve(m_lastCheckResult);
        }

        progress.val.finished().onReceive(this, [this, historyPath, historyRv, buff, resolve](const ProgressResult& res) {
            if (!res.ret) {
                m_lastCheckResult.ret = res.ret;
                (void)resolve(m_lastCheckResult);
                return;
            }

            // Successfully performed request, update history file...
            UpdateRequestHistory updateRequestHistory = historyRv.val;
            updateRequestHistory.previousRequestDay = QDate::currentDate();
            Ret writeHistoryRet = writeUpdateRequestHistory(historyPath, updateRequestHistory);
            if (!writeHistoryRet) {
                LOGE() << writeHistoryRet.toString();
            }

            if (m_jubelleFeed) {
                // Jubelle R3: Jubelle's own GitHub Releases. The stable feed is one
                // release; "Testing" releases (GitHub pre-releases) come as a list.
                // Drafts are never offered.
                QByteArray json = buff->data();
                RetVal<ReleaseInfo> releaseInfo = parseJubelleReleases(json, configuration()->allowUpdateOnPreRelease());

                if (!releaseInfo.ret) {
                    m_lastCheckResult.ret = releaseInfo.ret;
                    (void)resolve(m_lastCheckResult);
                    return;
                }

                // Jubelle versions are numbered 4.7.7.<build>; every part counts
                const std::string current = application()->version().toString().toStdString() + "."
                                            + application()->build().toStdString();
                if (jubelle::compareVersions(releaseInfo.val.version, current) <= 0) {
                    m_lastCheckResult.ret = make_ret(Err::NoUpdate);
                    (void)resolve(m_lastCheckResult);
                    return;
                }

                m_lastCheckResult = releaseInfo;
                (void)resolve(m_lastCheckResult);
                return;
            }

            // MuseScore's own update feed (kept unchanged)
            QByteArray json = buff->data();
            RetVal<ReleaseInfo> releaseInfo = parseRelease(json);

            if (!releaseInfo.ret) {
                m_lastCheckResult.ret = releaseInfo.ret;
                (void)resolve(m_lastCheckResult);
                return;
            }

            Version current(application()->fullVersion());
            Version update(releaseInfo.val.version);

            bool allowUpdateOnPreRelease = configuration()->allowUpdateOnPreRelease();
            bool isPreRelease = update.preRelease();

            if (!allowUpdateOnPreRelease && isPreRelease) {
                m_lastCheckResult.ret = make_ret(Err::NoUpdate);
                (void)resolve(m_lastCheckResult);
                return;
            }

            if (update <= current) {
                m_lastCheckResult.ret = make_ret(Err::NoUpdate);
                (void)resolve(m_lastCheckResult);
                return;
            }

            m_lastCheckResult = releaseInfo;

            downloadPreviousReleasesNotes(update, [this, resolve](const PrevReleasesNotesList& notes) {
                m_lastCheckResult.val.previousReleasesNotes = notes;
                (void)resolve(m_lastCheckResult);
            });
        });

        return Promise<RetVal<ReleaseInfo> >::dummy_result();
    });
}

const RetVal<ReleaseInfo>& AppUpdateService::lastCheckResult() const
{
    return m_lastCheckResult;
}

RetVal<Progress> AppUpdateService::downloadRelease()
{
    if (!m_networkManager) {
        m_networkManager = networkManagerCreator()->makeNetworkManager();
    }

    const ReleaseInfo info = m_lastCheckResult.val;
    const QUrl fileUrl = QUrl::fromUserInput(QString::fromStdString(info.fileUrl));
    auto buff = std::make_shared<QBuffer>();

    RetVal<Progress> downloadProgress = m_networkManager->get(fileUrl, buff);
    if (!downloadProgress.ret) {
        return RetVal<Progress>::make_ret(downloadProgress.ret);
    }

    m_updateProgress.start();

    m_updateProgress.canceled().onNotify(this, [this, downloadProgress]() {
        Progress mutProgress = downloadProgress.val;
        mutProgress.cancel();
        m_updateProgress.canceled().disconnect(this);
    });

    downloadProgress.val.progressChanged().onReceive(this, [this](int64_t current, int64_t total, const std::string& msg) {
        m_updateProgress.progress(current, total, msg);
    });

    downloadProgress.val.finished().onReceive(this, [this, info, buff](const ProgressResult& res) {
        if (!res.ret) {
            m_updateProgress.finish(ProgressResult::make_ret(res.ret));
            return;
        }

        // Jubelle R3: from Jubelle's releases, check the signature and the
        // checksum before the installer is saved where it can be run
        if (m_jubelleFeed) {
            jubelleVerifyAndSave(info, std::make_shared<QByteArray>(buff->data()));
            return;
        }

        const path_t installerPath = configuration()->updateDataPath() + "/" + info.fileName;
        fileSystem()->makePath(muse::io::absoluteDirpath(installerPath));

        const Ret ret = fileSystem()->writeFile(installerPath, ByteArray::fromQByteArrayNoCopy(buff->data()));
        if (!ret) {
            m_updateProgress.finish(ProgressResult::make_ret(ret));
            return;
        }

        m_updateProgress.finish(ProgressResult::make_ok(Val(installerPath)));
    });

    return RetVal<Progress>::make_ok(m_updateProgress);
}

RequestHeaders AppUpdateService::prepareHeaders(const UpdateRequestHistory& history) const
{
    RequestHeaders headers = configuration()->updateHeaders();

    if (history.isValid()) {
        std::string iwbString = history.installedWeekBeginning.toString(Qt::ISODate).toStdString();
        QByteArray iwbKey = QByteArray::fromStdString(INSTALLED_WEEK_BEGINNING_KEY.toStdString());
        headers.rawHeaders[iwbKey] = QByteArray::fromStdString(iwbString);

        std::string prdString = history.previousRequestDay.toString(Qt::ISODate).toStdString();
        QByteArray prdKey = QByteArray::fromStdString(PREVIOUS_REQUEST_DAY_KEY.toStdString());
        headers.rawHeaders[prdKey] = QByteArray::fromStdString(prdString);
    }

    return headers;
}

RetVal<AppUpdateService::UpdateRequestHistory> AppUpdateService::readUpdateRequestHistory(const io::path_t& path) const
{
    RetVal<ByteArray> rv = fileSystem()->readFile(path);
    if (!rv.ret) {
        return RetVal<UpdateRequestHistory>(rv.ret);
    }

    QJsonDocument historyDoc = QJsonDocument::fromJson(rv.val.toQByteArrayNoCopy());
    QJsonObject historyObject = historyDoc.object();

    UpdateRequestHistory updateRequestHistory;

    QString installedWeekBeginning = historyObject.value(INSTALLED_WEEK_BEGINNING_KEY).toString();
    updateRequestHistory.installedWeekBeginning = QDate::fromString(installedWeekBeginning, Qt::ISODate);

    QString previousRequestDay = historyObject.value(PREVIOUS_REQUEST_DAY_KEY).toString();
    updateRequestHistory.previousRequestDay = QDate::fromString(previousRequestDay, Qt::ISODate);

    return RetVal<UpdateRequestHistory>::make_ok(updateRequestHistory);
}

Ret AppUpdateService::writeUpdateRequestHistory(const io::path_t& path, const UpdateRequestHistory& updateRequestHistory)
{
    if (!updateRequestHistory.isValid()) {
        return make_ret(Ret::Code::UnknownError);
    }

    QJsonObject historyObject;

    const QDate& iwb = updateRequestHistory.installedWeekBeginning;
    const QDate& prd = updateRequestHistory.previousRequestDay;

    historyObject[INSTALLED_WEEK_BEGINNING_KEY] = iwb.toString(Qt::ISODate);
    historyObject[PREVIOUS_REQUEST_DAY_KEY] = prd.toString(Qt::ISODate);

    QByteArray byteArray = QJsonDocument(historyObject).toJson();

    return fileSystem()->writeFile(path, ByteArray::fromQByteArrayNoCopy(byteArray));
}

RetVal<ReleaseInfo> AppUpdateService::parseRelease(const QByteArray& json) const
{
    QJsonParseError err;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        return make_ret(Err::ReleaseInfoParseError, err.errorString().toStdString());
    }

    QJsonObject release = jsonDoc.object();
    QJsonObject assetObj = resolveReleaseAsset(release);
    if (assetObj.empty()) {
        return make_ret(Err::ReleaseInfoParseError, "No release asset");
    }

    QString tagName = release.value("tag_name").toString();
    QString version = tagName.replace("v", "");
    if (version.isEmpty()) {
        return make_ret(Err::ReleaseInfoParseError, "No release version");
    }

    RetVal<ReleaseInfo> result;
    result.ret = muse::make_ok();
    result.val.fileName = assetObj.value("name").toString().toStdString();
    result.val.fileUrl = assetObj.value("browser_download_url").toString().toStdString();
    result.val.version = version.toStdString();
    result.val.notes = release.value("bodyMarkdown").toString().toStdString();

    return result;
}

std::string AppUpdateService::platformFileSuffix() const
{
    switch (systemInfo()->productType()) {
    case ISystemInfo::ProductType::Windows: return "msi";
    case ISystemInfo::ProductType::MacOS: return "dmg";
    case ISystemInfo::ProductType::Linux: return "appimage";
    case ISystemInfo::ProductType::Unknown: break;
    }

    return "";
}

QJsonObject AppUpdateService::resolveReleaseAsset(const QJsonObject& release) const
{
    std::string fileSuffix = platformFileSuffix();
    ISystemInfo::ProductType productType = systemInfo()->productType();
    ISystemInfo::CpuArchitecture arch = systemInfo()->cpuArchitecture();

    QJsonArray assets = release.value("assets").toArray();
    for (const QJsonValue& asset : release.value("assetsNew").toArray()) {
        assets.push_back(asset);
    }

    for (const QJsonValue asset : assets) {
        QJsonObject assetObj = asset.toObject();

        QString name = assetObj.value("name").toString();
        if (io::suffix(name) != fileSuffix) {
            continue;
        }

        if (productType == ISystemInfo::ProductType::Linux) {
            if (arch != ISystemInfo::CpuArchitecture::Unknown && arch != assetArch(name)) {
                continue;
            }
        }

        return assetObj;
    }

    return QJsonObject();
}

void AppUpdateService::downloadPreviousReleasesNotes(const Version& updateVersion, const PrevReleaseNotesCallback& finished)
{
    QUrl url = QString::fromStdString(configuration()->previousAppReleasesNotesUrl());
    auto buff = std::make_shared<QBuffer>();
    RetVal<Progress> progress = m_networkManager->get(url, buff);

    if (!progress.ret) {
        LOGE() << "Failed to get previous release notes: " << progress.ret.toString();
        finished({});
        return;
    }

    progress.val.finished().onReceive(this, [this, updateVersion, buff, finished](const ProgressResult& res) {
        PrevReleasesNotesList result;

        DEFER {
            finished(result);
            return;
        };

        if (!res.ret) {
            LOGE() << "Failed to get previous release notes: " << res.ret.toString();
            return;
        }

        QByteArray json = buff->data();
        PrevReleasesNotesList previousReleasesNotes = parsePreviousReleasesNotes(json);
        if (previousReleasesNotes.empty()) {
            return;
        }

        Version currentVersion = Version(application()->fullVersion());

        for (const PrevReleaseNotes& releaseNotes : previousReleasesNotes) {
            Version previousVersion = Version(releaseNotes.version);
            if (updateVersion == previousVersion) {
                continue;
            }

            if (currentVersion < previousVersion) {
                String notesStr = String::fromStdString(releaseNotes.notes);
                if (notesStr.startsWith(u"###")) {
                    //! Release notes may be in the format of: ### MuseScore x.y.z is now available!\r\n...notes...\r\n
                    //! We need to remove the title of the release notes to get the actual notes.
                    static const std::regex titleRegex(R"(^###.*?\r\n)");
                    std::string notesWithoutTitle = notesStr.remove(titleRegex).toStdString();
                    result.emplace_back(releaseNotes.version, notesWithoutTitle);
                } else {
                    result.emplace_back(releaseNotes.version, releaseNotes.notes);
                }
            }
        }

        std::sort(result.begin(), result.end(), [](const PrevReleaseNotes& a,
                                                   const PrevReleaseNotes& b) {
            return Version(a.version) < Version(b.version);
        });
    });
}

void AppUpdateService::clear()
{
    m_lastCheckResult = RetVal<ReleaseInfo>::make_ok(ReleaseInfo());

#if !defined(Q_OS_LINUX)
    fileSystem()->remove(configuration()->updateDataPath());
#endif
}

// ---------------------------------------------------------------------------
// Jubelle R3 (decision D6): Jubelle's GitHub Releases, verified
// ---------------------------------------------------------------------------

RetVal<ReleaseInfo> AppUpdateService::parseJubelleReleases(const QByteArray& json, bool allowTesting) const
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        return make_ret(Err::ReleaseInfoParseError, err.errorString().toStdString());
    }

    QJsonArray releases;
    if (doc.isArray()) {
        releases = doc.array();
    } else if (doc.isObject()) {
        releases.append(doc.object());
    }

    RetVal<ReleaseInfo> best = make_ret(Err::NoUpdate);
    for (const QJsonValue& value : releases) {
        const QJsonObject release = value.toObject();
        if (release.value("draft").toBool()) {
            continue;
        }
        const bool testing = release.value("prerelease").toBool();
        if (testing && !allowTesting) {
            continue;
        }
        const QJsonObject installer = resolveReleaseAsset(release);
        if (installer.empty()) {
            continue;
        }
        std::string version = release.value("tag_name").toString().toStdString();
        if (!version.empty() && (version.front() == 'v' || version.front() == 'V')) {
            version.erase(0, 1);
        }
        if (version.empty() || (best.ret && jubelle::compareVersions(version, best.val.version) <= 0)) {
            continue;
        }

        ReleaseInfo info;
        info.version = version;
        info.fileName = installer.value("name").toString().toStdString();
        info.fileUrl = installer.value("browser_download_url").toString().toStdString();
        info.notes = release.value("body").toString().toStdString();
        info.additionInfo["jubelleTesting"] = Val(testing);
        for (const QJsonValue& a : release.value("assets").toArray()) {
            const QJsonObject asset = a.toObject();
            const QString name = asset.value("name").toString();
            const std::string url = asset.value("browser_download_url").toString().toStdString();
            if (name == "SHA256SUMS") {
                info.additionInfo["jubelleSumsUrl"] = Val(url);
            } else if (name == "SHA256SUMS.sig") {
                info.additionInfo["jubelleSigUrl"] = Val(url);
            }
        }
        if (info.additionInfo.find("jubelleSumsUrl") == info.additionInfo.end()
            || info.additionInfo.find("jubelleSigUrl") == info.additionInfo.end()) {
            LOGW() << "Jubelle release " << version << " has no signed checksums: not offered";
            continue;   // an unsigned release is never offered
        }
        best = RetVal<ReleaseInfo>::make_ok(info);
    }
    return best;
}

bool AppUpdateService::isJubelleReleasesUrl(const std::string& url)
{
    return QUrl(QString::fromStdString(url)).host().toLower() == "api.github.com";
}

static bool isGitHubDownload(const std::string& url)
{
    const QUrl u(QString::fromStdString(url));
    const QString host = u.host().toLower();
    return u.scheme() == "https" && (host == "github.com" || host.endsWith(".githubusercontent.com"));
}

void AppUpdateService::jubelleVerifyAndSave(const ReleaseInfo& info, std::shared_ptr<QByteArray> installer)
{
    auto fail = [this](const std::string& why) {
        LOGE() << "Jubelle update not verified: " << why;
        m_updateProgress.finish(ProgressResult::make_ret(make_ret(Err::JubelleNotVerified, why)));
    };

    auto get = [&](const char* key) {
        auto it = info.additionInfo.find(key);
        return it == info.additionInfo.end() ? std::string() : it->second.toString();
    };
    const std::string sumsUrl = get("jubelleSumsUrl");
    const std::string sigUrl = get("jubelleSigUrl");
    if (!isGitHubDownload(info.fileUrl) || !isGitHubDownload(sumsUrl) || !isGitHubDownload(sigUrl)) {
        fail(muse::trc("update", "The update didn't come from Jubelle's GitHub releases."));
        return;
    }
    if (jubelle::releasePublicKeyHex().empty()) {
        fail(muse::trc("update", "This build of Jubelle has no release key, so it can't check updates. Install new versions by hand."));
        return;
    }

    auto sums = std::make_shared<QBuffer>();
    RetVal<Progress> sumsProgress = m_networkManager->get(QUrl(QString::fromStdString(sumsUrl)), sums);
    if (!sumsProgress.ret) {
        fail(sumsProgress.ret.toString());
        return;
    }
    sumsProgress.val.finished().onReceive(this, [this, info, installer, sums, sigUrl, fail](const ProgressResult& res) {
        if (!res.ret) {
            fail(res.ret.toString());
            return;
        }
        auto sig = std::make_shared<QBuffer>();
        RetVal<Progress> sigProgress = m_networkManager->get(QUrl(QString::fromStdString(sigUrl)), sig);
        if (!sigProgress.ret) {
            fail(sigProgress.ret.toString());
            return;
        }
        sigProgress.val.finished().onReceive(this, [this, info, installer, sums, sig, fail](const ProgressResult& res2) {
            if (!res2.ret) {
                fail(res2.ret.toString());
                return;
            }
            const jubelle::Bytes installerBytes(installer->begin(), installer->end());
            const QByteArray sumsData = sums->data();
            const QByteArray sigData = sig->data();
            const jubelle::Verdict verdict = jubelle::verifyInstaller(installerBytes, info.fileName,
                                                                      std::string(sumsData.constData(), size_t(sumsData.size())),
                                                                      jubelle::Bytes(sigData.begin(), sigData.end()),
                                                                      jubelle::releasePublicKeyHex());
            switch (verdict) {
            case jubelle::Verdict::Ok:
                break;
            case jubelle::Verdict::NoReleaseKey:
                fail(muse::trc("update", "This build of Jubelle has no release key."));
                return;
            case jubelle::Verdict::BadSignature:
                fail(muse::trc("update", "The update's checksum list isn't signed with Jubelle's release key. Nothing was installed."));
                return;
            case jubelle::Verdict::NotListed:
                fail(muse::trc("update", "The installer isn't listed in the release's signed checksums. Nothing was installed."));
                return;
            case jubelle::Verdict::WrongChecksum:
                fail(muse::trc("update", "The downloaded installer is damaged or was changed. It was deleted and nothing was installed."));
                return;
            }

            // Verified: saved next to earlier downloads, which are kept so a
            // previous version can be reinstalled
            const path_t installerPath = configuration()->updateDataPath() + "/" + info.fileName;
            fileSystem()->makePath(muse::io::absoluteDirpath(installerPath));
            const Ret ret = fileSystem()->writeFile(installerPath, ByteArray(reinterpret_cast<const uint8_t*>(installer->constData()),
                                                                             size_t(installer->size())));
            if (!ret) {
                m_updateProgress.finish(ProgressResult::make_ret(ret));
                return;
            }
            m_updateProgress.finish(ProgressResult::make_ok(Val(installerPath)));
        });
    });
}

