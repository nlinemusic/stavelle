/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle G10: the YouTube connection's plain parts (no Qt, so they are tested
// on their own): Google's addresses, the one permission, the upload request's
// description, and safe storage of the sign-in token.
//
// The design approved on 17 Nov 2026: sign-in on Google's own page in the
// browser; one permission, "Manage your YouTube videos" (youtube.upload);
// Private by default; the token kept on this computer only (Windows
// Credential Manager), never in scores or projects; one button disconnects.

#pragma once

#include <string>

namespace mu::project::jubelle {
constexpr const char* GOOGLE_AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr const char* GOOGLE_TOKEN_URL = "https://oauth2.googleapis.com/token";
constexpr const char* GOOGLE_REVOKE_URL = "https://oauth2.googleapis.com/revoke";
//! the only permission Jubelle asks for: upload videos
constexpr const char* YOUTUBE_UPLOAD_SCOPE = "https://www.googleapis.com/auth/youtube.upload";
constexpr const char* YOUTUBE_UPLOAD_URL
    = "https://www.googleapis.com/upload/youtube/v3/videos?uploadType=resumable&part=snippet,status";

enum class Privacy {
    Private,
    Unlisted,
    Public,
};

const char* privacyName(Privacy p);   // "private", "unlisted", "public"

//! JSON text escaped for a string value (quotes, backslashes, control characters)
std::string jsonEscape(const std::string& utf8);

//! The video's description for the upload request: title, description, privacy, category Music.
//! YouTube limits: title 100 characters (no < or >), description 5000 bytes; longer text is cut.
std::string uploadMetadata(const std::string& title, const std::string& description, Privacy privacy);

//! The title YouTube will accept: < and > removed, at most 100 characters, never empty
std::string cleanTitle(const std::string& title);

//! "https://youtu.be/<id>"
std::string videoLink(const std::string& videoId);

// ---- the sign-in token, kept on this computer only ----

//! Windows: Credential Manager ("Jubelle YouTube"). Elsewhere: a file readable only by you in `fallbackDir`.
bool storeSecret(const std::string& name, const std::string& secret, const std::string& fallbackDir);
std::string readSecret(const std::string& name, const std::string& fallbackDir);
void removeSecret(const std::string& name, const std::string& fallbackDir);
}
