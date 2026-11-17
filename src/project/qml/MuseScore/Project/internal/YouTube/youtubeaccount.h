/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <functional>

#include <QObject>
#include <QPointer>
#include <QString>

class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace mu::project::jubelle {
//! Jubelle G10: the YouTube connection (design approved 17 Nov 2026).
//! Sign-in happens on Google's page in the browser (OAuth 2 with PKCE and a loopback
//! address on this computer); the only permission is youtube.upload. The refresh token is
//! kept in Windows Credential Manager. Uploads are resumable and Private unless chosen.
//! Nothing is sent anywhere except to Google, and only when you press a button.
class YouTubeAccount : public QObject
{
    Q_OBJECT

public:
    explicit YouTubeAccount(const QString& secretsDir, QObject* parent = nullptr);
    ~YouTubeAccount() override;

    //! Your own Google Cloud "Desktop app" client (Jubelle has no shared one)
    QString clientId() const;
    bool hasClient() const;
    void setClient(const QString& clientId, const QString& clientSecret);

    bool isConnected() const;
    bool isBusy() const { return m_busy; }

    void connectAccount();
    void disconnectAccount();

    //! privacy: 0 private, 1 unlisted, 2 public
    void upload(const QString& filePath, const QString& title, const QString& description, int privacy);
    void cancelUpload();

signals:
    void stateChanged();
    void message(const QString& text);
    void uploadProgress(qint64 sent, qint64 total);
    void uploadFinished(bool ok, const QString& linkOrError);

private:
    QOAuth2AuthorizationCodeFlow* makeFlow();
    void withFreshToken(std::function<void(const QString& accessToken)> then);
    void startUpload(const QString& token);
    void sendFile(const QString& token, const QUrl& sessionUrl);
    void fail(const QString& text);
    static QString googleError(QNetworkReply* reply);

    QString m_secretsDir;
    QOAuth2AuthorizationCodeFlow* m_flow = nullptr;
    QOAuthHttpServerReplyHandler* m_handler = nullptr;
    QNetworkAccessManager* m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile* m_file = nullptr;
    bool m_busy = false;
    bool m_uploading = false;

    QString m_path;
    QString m_title;
    QString m_description;
    int m_privacy = 0;
};
}
