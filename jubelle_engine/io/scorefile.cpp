/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 */
#include "scorefile.h"

#include <algorithm>
#include <cstdio>

namespace je::io {
namespace {
const char* STORE_TAG = "jubelleEngine";
const int STORE_VERSION = 1;

bool endsWith(const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }

std::string jsonEscape(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if ((unsigned char)c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            o += buf;
        } else {
            o += c;
        }
    }
    return o;
}

//! The "style" object of the store: a flat {"name":"raw XML text"} map (a tiny reader for our own format)
std::map<std::string, std::string> storeStyle(const std::string& json)
{
    std::map<std::string, std::string> out;
    const size_t at = json.find("\"style\"");
    if (at == std::string::npos) {
        return out;
    }
    size_t i = json.find('{', at);
    auto str = [&json](size_t& p, std::string& v) {
        if (p >= json.size() || json[p] != '"') {
            return false;
        }
        v.clear();
        for (++p; p < json.size() && json[p] != '"'; ++p) {
            if (json[p] == '\\' && p + 1 < json.size()) {
                ++p;
                if (json[p] == 'u' && p + 4 < json.size()) {
                    v += char(std::stoi(json.substr(p + 1, 4), nullptr, 16));
                    p += 4;
                    continue;
                }
            }
            v += json[p];
        }
        ++p;
        return true;
    };
    while (i != std::string::npos && i < json.size()) {
        i = json.find_first_of("\"}", i + 1);
        if (i == std::string::npos || json[i] == '}') {
            break;
        }
        std::string k, v;
        if (!str(i, k)) {
            break;
        }
        i = json.find('"', i);
        if (i == std::string::npos || !str(i, v)) {
            break;
        }
        out[k] = v;
        --i;
    }
    return out;
}
}

const std::vector<std::string>& jubelleStyleNames()
{
    static const std::vector<std::string> names {
        "jubelleCourtesyAccidentals", "jubelleCourtesyAccidentalsAllOctaves", "jubelleCourtesyAccidentalsBars",
        "jubelleCourtesyAccidentalsOtherStaves", "jubelleCourtesyAccidentalsParentheses", "jubelleMagneticLayout",
        "jubelleMagneticRows", "jubelleSidewaysNudge", "jubelleNudgeLimit", "jubelleCollisionAvoidance",
        "jubelleEngravingRules",
    };
    return names;
}

bool loadBytes(const std::string& bytes, bool plainMscx, ScoreFile& file, std::string& error)
{
    file = ScoreFile();
    file.plainMscx = plainMscx;
    if (plainMscx) {
        file.mainEntry = "score.mscx";
        return xml::parse(bytes, file.main, error);
    }
    if (!zip::read(bytes, file.entries, error)) {
        return false;
    }
    // the main score: as META-INF/container.xml says, else the first .mscx outside Excerpts/
    for (const zip::Entry& e : file.entries) {
        if (e.name == "META-INF/container.xml") {
            // the first listed .mscx outside Excerpts/ (the list also names style files and the parts)
            for (size_t p = e.data.find("full-path=\""); p != std::string::npos && file.mainEntry.empty();
                 p = e.data.find("full-path=\"", p + 1)) {
                const size_t q = e.data.find('"', p + 11);
                const std::string path = xml::decode(e.data.substr(p + 11, q - p - 11));
                if (endsWith(path, ".mscx") && path.rfind("Excerpts/", 0) != 0) {
                    file.mainEntry = path;
                }
            }
        }
    }
    if (file.mainEntry.empty()) {
        for (const zip::Entry& e : file.entries) {
            if (endsWith(e.name, ".mscx") && e.name.rfind("Excerpts/", 0) != 0) {
                file.mainEntry = e.name;
                break;
            }
        }
    }
    bool found = false;
    for (const zip::Entry& e : file.entries) {
        if (e.name == file.mainEntry) {
            if (!xml::parse(e.data, file.main, error)) {
                error = e.name + ": " + error;
                return false;
            }
            found = true;
        } else if (e.name.rfind("Excerpts/", 0) == 0 && endsWith(e.name, ".mscx")) {
            xml::Document d;
            if (!xml::parse(e.data, d, error)) {
                error = e.name + ": " + error;
                return false;
            }
            file.excerpts.emplace(e.name, std::move(d));
        }
    }
    if (!found) {
        error = "no score inside the archive";
        return false;
    }
    return true;
}

bool load(const std::string& path, ScoreFile& file, std::string& error)
{
    std::string bytes;
    if (!zip::readFile(path, bytes)) {
        error = "can't read the file";
        return false;
    }
    return loadBytes(bytes, endsWith(path, ".mscx"), file, error);
}

std::string saveBytes(const ScoreFile& file)
{
    if (file.plainMscx) {
        return xml::write(file.main);
    }
    std::vector<zip::Entry> out = file.entries;
    for (zip::Entry& e : out) {
        if (e.name == file.mainEntry) {
            e.data = xml::write(file.main);
        } else {
            auto it = file.excerpts.find(e.name);
            if (it != file.excerpts.end()) {
                e.data = xml::write(it->second);
            }
        }
    }
    return zip::write(out);
}

void makeArchive(ScoreFile& file, const std::string& scoreName)
{
    if (!file.plainMscx) {
        return;
    }
    file.plainMscx = false;
    file.mainEntry = scoreName;
    zip::Entry container;
    container.name = "META-INF/container.xml";
    container.data = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<container>\n  <rootfiles>\n    <rootfile full-path=\""
                     + xml::encode(scoreName) + "\"/>\n    </rootfiles>\n  </container>\n";
    zip::Entry score;
    score.name = scoreName;
    file.entries = { score, container };
}

bool save(const std::string& path, const ScoreFile& file)
{
    if (file.plainMscx && endsWith(path, ".mscz")) {
        ScoreFile archive = file;
        makeArchive(archive);
        return zip::writeFile(path, saveBytes(archive));
    }
    return zip::writeFile(path, saveBytes(file));
}

std::string metaTag(const ScoreFile& file, const std::string& name)
{
    const xml::Node* score = file.score();
    if (!score) {
        return std::string();
    }
    for (const xml::Node& c : score->children) {
        if (c.name == "metaTag" && c.attr("name") == name) {
            return c.text();
        }
    }
    return std::string();
}

void setMetaTag(ScoreFile& file, const std::string& name, const std::string& value)
{
    xml::Node* score = file.score();
    if (!score) {
        return;
    }
    size_t lastMeta = std::string::npos;
    for (size_t i = 0; i < score->children.size(); ++i) {
        xml::Node& c = score->children[i];
        if (c.name == "metaTag") {
            if (c.attr("name") == name) {
                c.raw = xml::encode(value);
                c.leaf = true;
                c.selfClosing = false;
                return;
            }
            lastMeta = i;
        }
    }
    xml::Node tag;
    tag.name = "metaTag";
    tag.attrs.emplace_back("name", xml::encode(name));
    tag.leaf = true;
    tag.raw = xml::encode(value);
    // after the meta tags, in name order as MuseScore writes them
    size_t at = lastMeta == std::string::npos ? 0 : lastMeta + 1;
    for (size_t i = 0; i < score->children.size(); ++i) {
        if (score->children[i].name == "metaTag" && score->children[i].attr("name") > name) {
            at = i;
            break;
        }
    }
    score->children.insert(score->children.begin() + std::ptrdiff_t(at), std::move(tag));
}

std::string updateEngineStore(ScoreFile& file)
{
    std::string json = "{\"version\":" + std::to_string(STORE_VERSION) + ",\"style\":{";
    bool first = true;
    if (const xml::Node* score = file.score()) {
        if (const xml::Node* style = score->child("Style")) {
            for (const std::string& n : jubelleStyleNames()) {
                if (const xml::Node* v = style->child(n)) {
                    json += std::string(first ? "" : ",") + "\"" + n + "\":\"" + jsonEscape(v->raw) + "\"";
                    first = false;
                }
            }
        }
    }
    json += "}}";
    // an existing store keeps its other members once there are any; version 1 holds the style mirror only
    setMetaTag(file, STORE_TAG, json);
    return json;
}

std::vector<std::string> restoreFromEngineStore(ScoreFile& file)
{
    std::vector<std::string> restored;
    const std::string json = metaTag(file, STORE_TAG);
    if (json.empty()) {
        return restored;
    }
    xml::Node* score = file.score();
    xml::Node* style = score ? score->child("Style") : nullptr;
    if (!style) {
        return restored;
    }
    if (style->leaf) {   // an empty <Style/>: make it a container
        style->leaf = false;
        style->selfClosing = false;
        style->raw.clear();
    }
    for (const auto& [name, raw] : storeStyle(json)) {
        if (std::find(jubelleStyleNames().begin(), jubelleStyleNames().end(), name) == jubelleStyleNames().end()) {
            continue;   // only Jubelle's own settings are ever restored
        }
        if (style->child(name)) {
            continue;   // a value in the file wins
        }
        xml::Node v;
        v.name = name;
        v.leaf = true;
        v.raw = raw;
        style->children.push_back(std::move(v));
        restored.push_back(name);
    }
    return restored;
}
}
