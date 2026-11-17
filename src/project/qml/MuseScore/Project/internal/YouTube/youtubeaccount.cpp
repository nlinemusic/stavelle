/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "youtubeaccount.h"

#include <functional>

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QSettings>
#include <QUrlQuery>

#include "translation.h"

#include "project/internal/youtubeupload.h"

#include "log.h"

using namespace mu::project::jubelle;

namespace {
const char* SETTINGS_CLIENT_ID = "jubelle/youtube/clientId";
const std::string SECRET_REFRESH = "youtube-refresh-token";
const std::string SECRET_CLIENT = "youtube-client-secret";
}

YouTubeAccount::YouTubeAccount(const QString& secretsDir, QObject* parent)
    : QObject(parent), m_secretsDir(secretsDir)
{
}

YouTubeAccount::~YouTubeAccount()
{
    cancelUpload();
}

QString YouTubeAccount::clientId() const
{
    return QSettings().value(SETTINGS_CLIENT_ID).toString();
}

bool YouTubeAccount::hasClient() const
{
    return !clientId().isEmpty() && !readSecret(SECRET_CLIENT, m_secretsDir.toStdString()).empty();
}

void YouTubeAccount::setClient(const QString& id, const QString& secret)
{
    QSettings().setValue(SETTINGS_CLIENT_ID, id.trimmed());
    if (!secret.trimmed().isEmpty()) {
        storeSecret(SECRET_CLIENT, secret.trimmed().toStdString(), m_secretsDir.toStdString());
    }
    // a different client means signing in again
    delete m_flow;
    m_flow = nullptr;
    emit stateChanged();
}

bool YouTubeAccount::isConnected() const
{
    return !readSecret(SECRET_REFRESH, m_secretsDir.toStdString()).empty();
}

QOAuth2AuthorizationCodeFlow* YouTubeAccount::makeFlow()
{
    if (m_flow) {
        return m_flow;
    }
    m_flow = new QOAuth2AuthorizationCodeFlow(this);
    m_flow->setAuthorizationUrl(QUrl(QString::fromLatin1(GOOGLE_AUTH_URL)));
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    m_flow->setTokenUrl(QUrl(QString::fromLatin1(GOOGLE_TOKEN_URL)));
    m_flow->setRequestedScopeTokens({ QByteArray(YOUTUBE_UPLOAD_SCOPE) });
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
#else
    m_flow->setAccessTokenUrl(QUrl(QString::fromLatin1(GOOGLE_TOKEN_URL)));
    m_flow->setScope(QString::fromLatin1(YOUTUBE_UPLOAD_SCOPE));
#endif
    m_flow->setClientIdentifier(clientId());
    m_flow->setClientIdentifierSharedKey(QString::fromStdString(readSecret(SECRET_CLIENT, m_secretsDir.toStdString())));

    // Google gives a refresh token (so you stay connected) only with these
    m_flow->setModifyParametersFunction([](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant>* params) {
        if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
            params->replace(QStringLiteral("access_type"), QStringLiteral("offline"));
            params->replace(QStringLiteral("prompt"), QStringLiteral("consent"));
        }
    });

    connect(m_flow, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser, this, [](const QUrl& url) {
        QDesktopServices::openUrl(url);
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    connect(m_flow, &QOAuth2AuthorizationCodeFlow::serverReportedErrorOccurred, this,
            [this](const QString& error, const QString& description, const QUrl&) {
        fail(muse::qtrc("project", "Google refused the sign-in: %1").arg(description.isEmpty() ? error : description));
    });
#else
    connect(m_flow, &QOAuth2AuthorizationCodeFlow::error, this, [this](const QString& error, const QString& description, const QUrl&) {
        fail(muse::qtrc("project", "Google refused the sign-in: %1").arg(description.isEmpty() ? error : description));
    });
#endif
    return m_flow;
}

void YouTubeAccount::connectAccount()
{
    if (!hasClient()) {
        fail(muse::qtrc("project", "Set up Google access first (your client ID and secret)."));
        return;
    }
    if (m_busy) {
        return;
    }
    QOAuth2AuthorizationCodeFlow* flow = makeFlow();
    if (!m_handler) {
        // a one-time address on this computer where Google sends you back after signing in
        m_handler = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, 0, this);
    }
    if (!m_handler->isListening() && !m_handler->listen(QHostAddress::LocalHost, 0)) {
        fail(muse::qtrc("project", "Jubelle could not open a local address to finish signing in."));
        return;
    }
    flow->setReplyHandler(m_handler);

    m_busy = true;
    emit stateChanged();
    emit message(muse::qtrc("project", "Your browser has opened Google's sign-in page. Sign in there, then come back to Jubelle."));

    QObject* once = new QObject(this);
    connect(flow, &QOAuth2AuthorizationCodeFlow::granted, once, [this, flow, once]() {
        once->deleteLater();
        m_handler->close();
        m_busy = false;
        if (flow->refreshToken().isEmpty()) {
            fail(muse::qtrc("project", "Google did not give Jubelle a lasting sign-in. Please try again."));
            return;
        }
        storeSecret(SECRET_REFRESH, flow->refreshToken().toStdString(), m_secretsDir.toStdString());
        emit stateChanged();
        emit message(muse::qtrc("project", "Connected. Jubelle can now upload videos to your channel."));
    });
    flow->grant();
}

void YouTubeAccount::disconnectAccount()
{
    cancelUpload();
    const std::string refresh = readSecret(SECRET_REFRESH, m_secretsDir.toStdString());
    if (!refresh.empty()) {
        // tell Google too, so the permission is withdrawn there as well
        if (!m_net) {
            m_net = new QNetworkAccessManager(this);
        }
        QNetworkRequest req(QUrl(QString::fromLatin1(GOOGLE_REVOKE_URL)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        QUrlQuery body;
        body.addQueryItem(QStringLiteral("token"), QString::fromStdString(refresh));
        QNetworkReply* r = m_net->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
        connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    }
    removeSecret(SECRET_REFRESH, m_secretsDir.toStdString());
    delete m_flow;
    m_flow = nullptr;
    emit stateChanged();
    emit message(muse::qtrc("project", "Disconnected. Jubelle no longer keeps a sign-in for YouTube."));
}

void YouTubeAccount::withFreshToken(std::function<void(const QString&)> then)
{
    QOAuth2AuthorizationCodeFlow* flow = makeFlow();
    flow->setRefreshToken(QString::fromStdString(readSecret(SECRET_REFRESH, m_secretsDir.toStdString())));
    QObject* once = new QObject(this);
    connect(flow, &QAbstractOAuth::tokenChanged, once, [flow, once, then](const QString&) {
        once->deleteLater();
        then(flow->token());
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    connect(flow, &QAbstractOAuth::requestFailed, once, [this, once](QAbstractOAuth::Error) {
        once->deleteLater();
        fail(muse::qtrc("project", "The YouTube sign-in has expired or was withdrawn. Connect again."));
        removeSecret(SECRET_REFRESH, m_secretsDir.toStdString());
        emit stateChanged();
    });
    flow->refreshTokens();
#else
    flow->refreshAccessToken();
#endif
}

void YouTubeAccount::upload(const QString& filePath, const QString& title, const QString& description, int privacy)
{
    if (m_busy) {
        return;
    }
    if (!isConnected()) {
        fail(muse::qtrc("project", "Connect your YouTube account first."));
        return;
    }
    if (!QFileInfo(filePath).isFile()) {
        fail(muse::qtrc("project", "The video file is missing."));
        return;
    }
    m_path = filePath;
    m_title = title;
    m_description = description;
    m_privacy = privacy;
    m_busy = true;
    m_uploading = true;
    emit stateChanged();
    emit message(muse::qtrc("project", "Starting the upload…"));
    withFreshToken([this](const QString& token) { startUpload(token); });
}

void YouTubeAccount::startUpload(const QString& token)
{
    if (!m_net) {
        m_net = new QNetworkAccessManager(this);
    }
    const Privacy p = m_privacy == 2 ? Privacy::Public : m_privacy == 1 ? Privacy::Unlisted : Privacy::Private;
    const std::string meta = uploadMetadata(m_title.toStdString(), m_description.toStdString(), p);

    QNetworkRequest req(QUrl(QString::fromLatin1(YOUTUBE_UPLOAD_URL)));
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    req.setRawHeader("X-Upload-Content-Type", "video/mp4");
    req.setRawHeader("X-Upload-Content-Length", QByteArray::number(QFileInfo(m_path).size()));

    m_reply = m_net->post(req, QByteArray::fromStdString(meta));
    connect(m_reply, &QNetworkReply::finished, this, [this, token]() {
        QNetworkReply* r = m_reply;
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl session = QUrl(QString::fromUtf8(r->rawHeader("Location")));
        if (status != 200 || session.isEmpty()) {
            fail(muse::qtrc("project", "YouTube did not accept the upload: %1").arg(googleError(r)));
            return;
        }
        sendFile(token, session);
    });
}

void YouTubeAccount::sendFile(const QString& token, const QUrl& sessionUrl)
{
    m_file = new QFile(m_path, this);
    if (!m_file->open(QIODevice::ReadOnly)) {
        fail(muse::qtrc("project", "The video file could not be read."));
        return;
    }
    QNetworkRequest req(sessionUrl);
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("video/mp4"));
    req.setHeader(QNetworkRequest::ContentLengthHeader, m_file->size());

    m_reply = m_net->put(req, m_file);
    connect(m_reply, &QNetworkReply::uploadProgress, this, &YouTubeAccount::uploadProgress);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* r = m_reply;
        r->deleteLater();
        if (m_file) {
            m_file->close();
            m_file->deleteLater();
            m_file = nullptr;
        }
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = r->readAll();
        m_busy = false;
        m_uploading = false;
        emit stateChanged();
        if (r->error() == QNetworkReply::OperationCanceledError) {
            emit uploadFinished(false, muse::qtrc("project", "Upload cancelled."));
            return;
        }
        const QString id = QJsonDocument::fromJson(body).object().value(QStringLiteral("id")).toString();
        if ((status == 200 || status == 201) && !id.isEmpty()) {
            emit uploadFinished(true, QString::fromStdString(videoLink(id.toStdString())));
        } else {
            const QJsonObject err = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toObject();
            const QString text = err.value(QStringLiteral("message")).toString();
            emit uploadFinished(false, muse::qtrc("project", "The upload did not finish: %1")
                                .arg(text.isEmpty() ? r->errorString() : text));
        }
    });
}

void YouTubeAccount::cancelUpload()
{
    if (m_reply) {
        m_reply->abort();
    }
}

void YouTubeAccount::fail(const QString& text)
{
    LOGE() << text;
    m_busy = false;
    if (m_handler && m_handler->isListening()) {
        m_handler->close();
    }
    emit stateChanged();
    emit message(text);
    if (m_uploading) {
        m_uploading = false;
        emit uploadFinished(false, text);
    }
}

QString YouTubeAccount::googleError(QNetworkReply* reply)
{
    const QJsonObject err = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("error")).toObject();
    const QString text = err.value(QStringLiteral("message")).toString();
    return text.isEmpty() ? reply->errorString() : text;
}
