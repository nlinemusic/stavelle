/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
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
#include "projectinfo.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace mu::project::projectinfo {
static const char* INFO_DIR = ".jubelle";
static const char* INFO_FILE = "project.json";
static const char* TAGS_FILE = ".jubelle-tags.json";
static const char* CARD_IMAGE_STEM = "card-image";

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static fs::path toPath(const std::string& utf8)
{
    return fs::u8path(utf8);
}

static std::string toUtf8(const fs::path& path)
{
    return path.u8string();
}

static Result fail(const std::string& error)
{
    Result r;
    r.error = error;
    return r;
}

static Result success(const fs::path& path)
{
    Result r;
    r.ok = true;
    r.path = toUtf8(path);
    return r;
}

static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return std::string();
    }
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool containsNoCase(const std::vector<std::string>& list, const std::string& value)
{
    const std::string v = lower(value);
    return std::any_of(list.begin(), list.end(), [&v](const std::string& s) { return lower(s) == v; });
}

static bool isDir(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

static bool isFile(const fs::path& p)
{
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

static void setHidden(const fs::path& p)
{
#ifdef _WIN32
    DWORD attrs = GetFileAttributesW(p.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(p.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
    }
#else
    (void)p; // a leading dot already hides it on macOS
#endif
}

static fs::path infoDir(const std::string& projectFolder)
{
    return toPath(projectFolder) / INFO_DIR;
}

static bool ensureInfoDir(const std::string& projectFolder)
{
    const fs::path dir = infoDir(projectFolder);
    if (isDir(dir)) {
        return true;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec || !isDir(dir)) {
        return false;
    }
    setHidden(dir);
    return true;
}

//! Writes `content` to `target` through a temporary file in the same folder.
static bool writeFileSafely(const fs::path& target, const std::string& content)
{
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << content;
        out.flush();
        if (!out) {
            out.close();
            std::error_code ignore;
            fs::remove(tmp, ignore);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return false;
    }
    return true;
}

static bool readFile(const fs::path& p, std::string& out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// ---------------------------------------------------------------------------
// Minimal JSON (objects, arrays, strings, numbers, true/false/null)
// ---------------------------------------------------------------------------

namespace json {
struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    const Value* get(const std::string& key) const
    {
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }
};

class Parser
{
public:
    explicit Parser(const std::string& text)
        : m_s(text) {}

    bool parse(Value& out)
    {
        skip();
        if (!value(out)) {
            return false;
        }
        skip();
        return m_i == m_s.size();
    }

private:
    void skip()
    {
        while (m_i < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_i]))) {
            ++m_i;
        }
    }

    bool literal(const char* word)
    {
        size_t n = std::char_traits<char>::length(word);
        if (m_s.compare(m_i, n, word) == 0) {
            m_i += n;
            return true;
        }
        return false;
    }

    static void appendUtf8(std::string& out, unsigned code)
    {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool hex4(unsigned& code)
    {
        if (m_i + 4 > m_s.size()) {
            return false;
        }
        code = 0;
        for (int k = 0; k < 4; ++k) {
            char c = m_s[m_i++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }

    bool str(std::string& out)
    {
        if (m_i >= m_s.size() || m_s[m_i] != '"') {
            return false;
        }
        ++m_i;
        while (m_i < m_s.size()) {
            char c = m_s[m_i++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_i >= m_s.size()) {
                return false;
            }
            char e = m_s[m_i++];
            switch (e) {
            case '"': out += '"';
                break;
            case '\\': out += '\\';
                break;
            case '/': out += '/';
                break;
            case 'b': out += '\b';
                break;
            case 'f': out += '\f';
                break;
            case 'n': out += '\n';
                break;
            case 'r': out += '\r';
                break;
            case 't': out += '\t';
                break;
            case 'u': {
                unsigned code = 0;
                if (!hex4(code)) {
                    return false;
                }
                if (code >= 0xD800 && code <= 0xDBFF && m_s.compare(m_i, 2, "\\u") == 0) {
                    m_i += 2;
                    unsigned low = 0;
                    if (!hex4(low)) {
                        return false;
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                appendUtf8(out, code);
            } break;
            default:
                return false;
            }
        }
        return false;
    }

    bool value(Value& out)
    {
        if (m_i >= m_s.size() || ++m_depth > 64) {
            return false;
        }
        bool ok = false;
        char c = m_s[m_i];
        if (c == '{') {
            out.type = Value::Type::Object;
            ++m_i;
            skip();
            if (m_i < m_s.size() && m_s[m_i] == '}') {
                ++m_i;
                ok = true;
            } else {
                while (true) {
                    skip();
                    std::string key;
                    if (!str(key)) {
                        break;
                    }
                    skip();
                    if (m_i >= m_s.size() || m_s[m_i] != ':') {
                        break;
                    }
                    ++m_i;
                    skip();
                    Value v;
                    if (!value(v)) {
                        break;
                    }
                    out.object[key] = std::move(v);
                    skip();
                    if (m_i < m_s.size() && m_s[m_i] == ',') {
                        ++m_i;
                        continue;
                    }
                    if (m_i < m_s.size() && m_s[m_i] == '}') {
                        ++m_i;
                        ok = true;
                    }
                    break;
                }
            }
        } else if (c == '[') {
            out.type = Value::Type::Array;
            ++m_i;
            skip();
            if (m_i < m_s.size() && m_s[m_i] == ']') {
                ++m_i;
                ok = true;
            } else {
                while (true) {
                    skip();
                    Value v;
                    if (!value(v)) {
                        break;
                    }
                    out.array.push_back(std::move(v));
                    skip();
                    if (m_i < m_s.size() && m_s[m_i] == ',') {
                        ++m_i;
                        continue;
                    }
                    if (m_i < m_s.size() && m_s[m_i] == ']') {
                        ++m_i;
                        ok = true;
                    }
                    break;
                }
            }
        } else if (c == '"') {
            out.type = Value::Type::String;
            ok = str(out.string);
        } else if (literal("true")) {
            out.type = Value::Type::Bool;
            out.boolean = true;
            ok = true;
        } else if (literal("false")) {
            out.type = Value::Type::Bool;
            ok = true;
        } else if (literal("null")) {
            ok = true;
        } else {
            size_t start = m_i;
            while (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == '-'
                                        || m_s[m_i] == '+' || m_s[m_i] == '.' || m_s[m_i] == 'e' || m_s[m_i] == 'E')) {
                ++m_i;
            }
            if (m_i > start) {
                std::istringstream ns(m_s.substr(start, m_i - start));
                ns.imbue(std::locale::classic());
                ns >> out.number;
                out.type = Value::Type::Number;
                ok = !ns.fail();
            }
        }
        --m_depth;
        return ok;
    }

    const std::string& m_s;
    size_t m_i = 0;
    int m_depth = 0;
};

static std::string quote(const std::string& s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\"";
            break;
        case '\\': out += "\\\\";
            break;
        case '\n': out += "\\n";
            break;
        case '\r': out += "\\r";
            break;
        case '\t': out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += "\"";
    return out;
}

static std::string number(double v)
{
    std::ostringstream ns;
    ns.imbue(std::locale::classic());
    ns.precision(6);
    ns << v;
    return ns.str();
}

static std::string stringArray(const std::vector<std::string>& list)
{
    std::string out = "[";
    for (size_t i = 0; i < list.size(); ++i) {
        out += (i ? ", " : "") + quote(list[i]);
    }
    return out + "]";
}

static std::vector<std::string> toStrings(const Value* v)
{
    std::vector<std::string> out;
    if (!v || v->type != Value::Type::Array) {
        return out;
    }
    for (const Value& item : v->array) {
        if (item.type == Value::Type::String) {
            out.push_back(item.string);
        }
    }
    return out;
}
}

// ---------------------------------------------------------------------------
// Reading and writing project.json
// ---------------------------------------------------------------------------

static double clampPercent(double v)
{
    if (!(v == v)) { // NaN
        return 50.0;
    }
    return std::min(100.0, std::max(0.0, v));
}

static std::vector<std::string> cleanList(const std::vector<std::string>& in)
{
    std::vector<std::string> out;
    for (const std::string& s : in) {
        std::string t = trim(s);
        if (!t.empty() && !containsNoCase(out, t)) {
            out.push_back(t);
        }
    }
    return out;
}

ProjectInfo read(const std::string& projectFolder)
{
    ProjectInfo info;
    std::string text;
    if (!readFile(infoDir(projectFolder) / INFO_FILE, text)) {
        return info;
    }

    json::Value root;
    if (!json::Parser(text).parse(root) || root.type != json::Value::Type::Object) {
        return info;
    }

    info.extraCategories = cleanList(json::toStrings(root.get("categories")));
    info.tags = cleanList(json::toStrings(root.get("tags")));

    const json::Value* card = root.get("cardImage");
    if (card && card->type == json::Value::Type::Object) {
        const json::Value* file = card->get("file");
        if (file && file->type == json::Value::Type::String && !file->string.empty()
            && file->string.find('/') == std::string::npos && file->string.find('\\') == std::string::npos) {
            CardImage img;
            img.fileName = file->string;
            const json::Value* x = card->get("x");
            const json::Value* y = card->get("y");
            img.x = clampPercent(x && x->type == json::Value::Type::Number ? x->number : 50.0);
            img.y = clampPercent(y && y->type == json::Value::Type::Number ? y->number : 50.0);
            info.cardImage = img;
        }
    }

    return info;
}

Result write(const std::string& projectFolder, const ProjectInfo& info)
{
    if (!projectlibrary::isProjectFolder(projectFolder)) {
        return fail("This folder is not a Jubelle project: " + projectFolder);
    }
    if (!ensureInfoDir(projectFolder)) {
        return fail("The project information folder could not be created.");
    }

    std::string text = "{\n";
    text += "  \"version\": 1,\n";
    text += "  \"categories\": " + json::stringArray(cleanList(info.extraCategories)) + ",\n";
    text += "  \"tags\": " + json::stringArray(cleanList(info.tags));
    if (info.cardImage) {
        text += ",\n  \"cardImage\": { \"file\": " + json::quote(info.cardImage->fileName)
                + ", \"x\": " + json::number(clampPercent(info.cardImage->x))
                + ", \"y\": " + json::number(clampPercent(info.cardImage->y)) + " }";
    }
    text += "\n}\n";

    const fs::path target = infoDir(projectFolder) / INFO_FILE;
    if (!writeFileSafely(target, text)) {
        return fail("The project information could not be saved.");
    }
    return success(target);
}

// ---------------------------------------------------------------------------
// Categories
// ---------------------------------------------------------------------------

std::string homeCategory(const std::string& projectFolder)
{
    return toUtf8(toPath(projectFolder).parent_path().filename());
}

std::vector<std::string> allProjects(const std::string& mainFolder)
{
    std::vector<std::string> result;
    const fs::path main = toPath(mainFolder);
    for (const std::string& category : projectlibrary::categories(mainFolder)) {
        for (const std::string& name : projectlibrary::projects(mainFolder, category)) {
            result.push_back(toUtf8(main / toPath(category) / toPath(name)));
        }
    }
    return result;
}

std::vector<std::string> categoriesOf(const std::string& projectFolder, const std::string& mainFolder)
{
    std::vector<std::string> existing = projectlibrary::categories(mainFolder);
    std::vector<std::string> result { homeCategory(projectFolder) };
    for (const std::string& c : read(projectFolder).extraCategories) {
        if (std::find(existing.begin(), existing.end(), c) != existing.end()
            && std::find(result.begin(), result.end(), c) == result.end()) {
            result.push_back(c);
        }
    }
    return result;
}

std::vector<std::string> projectsIn(const std::string& mainFolder, const std::string& category)
{
    std::vector<std::string> result;
    for (const std::string& project : allProjects(mainFolder)) {
        if (homeCategory(project) == category) {
            result.push_back(project);
            continue;
        }
        const std::vector<std::string> extra = read(project).extraCategories;
        if (std::find(extra.begin(), extra.end(), category) != extra.end()) {
            result.push_back(project);
        }
    }
    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        return lower(toUtf8(toPath(a).filename())) < lower(toUtf8(toPath(b).filename()));
    });
    return result;
}

Result setCategories(const std::string& projectFolder, const std::string& mainFolder,
                     const std::vector<std::string>& wanted)
{
    if (!projectlibrary::isProjectFolder(projectFolder)) {
        return fail("This folder is not a Jubelle project: " + projectFolder);
    }

    std::vector<std::string> existing = projectlibrary::categories(mainFolder);
    std::vector<std::string> list;
    for (const std::string& c : wanted) {
        if (std::find(existing.begin(), existing.end(), c) == existing.end()) {
            return fail("The category \"" + c + "\" was not found.");
        }
        if (std::find(list.begin(), list.end(), c) == list.end()) {
            list.push_back(c);
        }
    }
    if (list.empty()) {
        return fail("A score needs at least one category.");
    }

    std::string folder = projectFolder;
    const std::string home = homeCategory(projectFolder);
    if (std::find(list.begin(), list.end(), home) == list.end()) {
        Result moved = projectlibrary::moveProject(projectFolder, mainFolder, list.front());
        if (!moved.ok) {
            return moved;
        }
        folder = moved.path;
    }

    const std::string newHome = homeCategory(folder);
    ProjectInfo info = read(folder);
    info.extraCategories.clear();
    for (const std::string& c : list) {
        if (c != newHome) {
            info.extraCategories.push_back(c);
        }
    }
    Result saved = write(folder, info);
    if (!saved.ok) {
        return saved;
    }
    return success(toPath(folder));
}

Result moveToCategory(const std::string& projectFolder, const std::string& mainFolder, const std::string& category)
{
    Result moved = projectlibrary::moveProject(projectFolder, mainFolder, category);
    if (!moved.ok) {
        return moved;
    }

    ProjectInfo info = read(moved.path);
    auto it = std::find(info.extraCategories.begin(), info.extraCategories.end(), category);
    if (it != info.extraCategories.end()) {
        info.extraCategories.erase(it);
        Result saved = write(moved.path, info);
        if (!saved.ok) {
            return saved;
        }
    }
    return moved;
}

bool isCategoryInUse(const std::string& mainFolder, const std::string& name)
{
    std::error_code ec;
    const fs::path dir = toPath(mainFolder) / toPath(name);
    if (isDir(dir) && !fs::is_empty(dir, ec)) {
        return true;
    }
    for (const std::string& project : allProjects(mainFolder)) {
        const std::vector<std::string> extra = read(project).extraCategories;
        if (std::find(extra.begin(), extra.end(), name) != extra.end()) {
            return true;
        }
    }
    return false;
}

Result renameCategory(const std::string& mainFolder, const std::string& oldName, const std::string& newName)
{
    Result renamed = projectlibrary::renameCategory(mainFolder, oldName, newName);
    if (!renamed.ok || oldName == newName) {
        return renamed;
    }

    std::string problems;
    for (const std::string& project : allProjects(mainFolder)) {
        ProjectInfo info = read(project);
        bool changed = false;
        for (std::string& c : info.extraCategories) {
            if (c == oldName) {
                c = newName;
                changed = true;
            }
        }
        if (changed && !write(project, info).ok) {
            problems += "\n" + project;
        }
    }

    if (!problems.empty()) {
        renamed.error = "The category was renamed, but these projects could not be updated:" + problems;
    }
    return renamed;
}

Result deleteCategory(const std::string& mainFolder, const std::string& name)
{
    for (const std::string& project : allProjects(mainFolder)) {
        const std::vector<std::string> extra = read(project).extraCategories;
        if (std::find(extra.begin(), extra.end(), name) != extra.end()) {
            return fail("The category \"" + name + "\" still lists scores. "
                        "Remove them from this category first, then delete it.");
        }
    }
    return projectlibrary::deleteCategory(mainFolder, name);
}

// ---------------------------------------------------------------------------
// Tags
// ---------------------------------------------------------------------------

static std::vector<std::string> readTagFile(const std::string& mainFolder)
{
    std::string text;
    if (!readFile(toPath(mainFolder) / TAGS_FILE, text)) {
        return {};
    }
    json::Value root;
    if (!json::Parser(text).parse(root) || root.type != json::Value::Type::Object) {
        return {};
    }
    return cleanList(json::toStrings(root.get("tags")));
}

static bool writeTagFile(const std::string& mainFolder, const std::vector<std::string>& tags)
{
    const fs::path target = toPath(mainFolder) / TAGS_FILE;
    const bool existed = isFile(target);
    std::string text = "{\n  \"version\": 1,\n  \"tags\": " + json::stringArray(cleanList(tags)) + "\n}\n";
    if (!writeFileSafely(target, text)) {
        return false;
    }
    if (!existed) {
        setHidden(target);
    }
    return true;
}

std::vector<std::string> tagLibrary(const std::string& mainFolder)
{
    std::vector<std::string> tags = readTagFile(mainFolder);
    for (const std::string& project : allProjects(mainFolder)) {
        for (const std::string& t : read(project).tags) {
            if (!containsNoCase(tags, t)) {
                tags.push_back(t);
            }
        }
    }
    std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) {
        return lower(a) < lower(b);
    });
    return tags;
}

Result addTagToLibrary(const std::string& mainFolder, const std::string& tag)
{
    const std::string t = trim(tag);
    if (t.empty()) {
        return fail("A tag needs a name.");
    }
    if (!isDir(toPath(mainFolder))) {
        return fail("The projects folder was not found: " + mainFolder);
    }
    std::vector<std::string> tags = readTagFile(mainFolder);
    if (!containsNoCase(tags, t)) {
        tags.push_back(t);
        if (!writeTagFile(mainFolder, tags)) {
            return fail("The tag could not be saved.");
        }
    }
    return success(toPath(mainFolder) / TAGS_FILE);
}

Result setTags(const std::string& projectFolder, const std::string& mainFolder, const std::vector<std::string>& wanted)
{
    // Reuse the spelling of a tag that already exists ("vocal" -> "Vocal").
    const std::vector<std::string> library = tagLibrary(mainFolder);
    std::vector<std::string> tags;
    for (const std::string& w : cleanList(wanted)) {
        std::string spelled = w;
        for (const std::string& known : library) {
            if (lower(known) == lower(w)) {
                spelled = known;
                break;
            }
        }
        tags.push_back(spelled);
        addTagToLibrary(mainFolder, spelled);
    }

    ProjectInfo info = read(projectFolder);
    info.tags = tags;
    return write(projectFolder, info);
}

bool matchesTags(const std::vector<std::string>& projectTags, const std::vector<std::string>& selected, TagMatch mode)
{
    if (selected.empty()) {
        return true;
    }
    if (mode == TagMatch::Any) {
        return std::any_of(selected.begin(), selected.end(), [&](const std::string& s) { return containsNoCase(projectTags, s); });
    }
    return std::all_of(selected.begin(), selected.end(), [&](const std::string& s) { return containsNoCase(projectTags, s); });
}

// ---------------------------------------------------------------------------
// Card image
// ---------------------------------------------------------------------------

static void removeOtherCardImages(const std::string& projectFolder, const std::string& keep)
{
    std::error_code ec;
    for (fs::directory_iterator it(infoDir(projectFolder), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = toUtf8(it->path().filename());
        if (name.rfind(CARD_IMAGE_STEM, 0) == 0 && name != keep) {
            std::error_code ignore;
            fs::remove(it->path(), ignore);
        }
    }
}

Result setCardImage(const std::string& projectFolder, const std::string& preparedImagePath, double x, double y)
{
    if (!projectlibrary::isProjectFolder(projectFolder)) {
        return fail("This folder is not a Jubelle project: " + projectFolder);
    }
    const fs::path source = toPath(preparedImagePath);
    if (!isFile(source)) {
        return fail("The image file was not found: " + preparedImagePath);
    }

    std::string ext = lower(toUtf8(source.extension()));
    if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".webp") {
        return fail("Card images must be JPG, PNG or WebP files.");
    }
    if (ext == ".jpeg") {
        ext = ".jpg";
    }
    if (!ensureInfoDir(projectFolder)) {
        return fail("The project information folder could not be created.");
    }

    const std::string fileName = std::string(CARD_IMAGE_STEM) + ext;
    const fs::path target = infoDir(projectFolder) / fileName;
    fs::path tmp = target;
    tmp += ".tmp";

    std::error_code ec;
    fs::copy_file(source, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return fail("The image could not be copied (" + ec.message() + ").");
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return fail("The image could not be saved (" + ec.message() + ").");
    }

    ProjectInfo info = read(projectFolder);
    info.cardImage = CardImage { fileName, x, y };
    Result saved = write(projectFolder, info);
    if (!saved.ok) {
        return saved;
    }

    removeOtherCardImages(projectFolder, fileName); // only one card image per project
    return success(target);
}

Result setCardImagePosition(const std::string& projectFolder, double x, double y)
{
    ProjectInfo info = read(projectFolder);
    if (!info.cardImage) {
        return fail("This project has no card image.");
    }
    info.cardImage->x = x;
    info.cardImage->y = y;
    return write(projectFolder, info);
}

Result removeCardImage(const std::string& projectFolder)
{
    ProjectInfo info = read(projectFolder);
    if (info.cardImage) {
        info.cardImage.reset();
        Result saved = write(projectFolder, info);
        if (!saved.ok) {
            return saved;
        }
    }
    removeOtherCardImages(projectFolder, std::string());
    return success(toPath(projectFolder));
}

std::string cardImagePath(const std::string& projectFolder)
{
    const ProjectInfo info = read(projectFolder);
    if (!info.cardImage) {
        return std::string();
    }
    const fs::path p = infoDir(projectFolder) / toPath(info.cardImage->fileName);
    return isFile(p) ? toUtf8(p) : std::string();
}

// ---------------------------------------------------------------------------
// Recent
// ---------------------------------------------------------------------------

static int64_t fileTimeSeconds(const fs::path& p, bool& ok)
{
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    ok = !ec;
    if (ec) {
        return 0;
    }
    // Convert to the system clock portably (file_clock has no to_sys in C++17).
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

std::vector<RecentScore> recentScores(const std::string& mainFolder, size_t limit)
{
    std::vector<RecentScore> all;
    for (const std::string& project : allProjects(mainFolder)) {
        const fs::path score = toPath(projectlibrary::mainScorePath(project));
        if (!isFile(score)) {
            continue; // a project without its score yet is not "recently saved"
        }
        bool ok = false;
        int64_t t = fileTimeSeconds(score, ok);
        if (!ok) {
            continue;
        }
        all.push_back(RecentScore { project, toUtf8(score), t });
    }

    std::stable_sort(all.begin(), all.end(), [](const RecentScore& a, const RecentScore& b) {
        if (a.savedTime != b.savedTime) {
            return a.savedTime > b.savedTime;
        }
        return lower(a.scorePath) < lower(b.scorePath);
    });

    if (all.size() > limit) {
        all.resize(limit);
    }
    return all;
}

std::vector<VideoFile> videoExports(const std::string& projectFolder)
{
    std::vector<VideoFile> result;
    const fs::path folder = toPath(projectFolder) / toPath("Video Exports");
    if (!isDir(folder)) {
        return result;
    }

    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        if (!isFile(p)) {
            continue;
        }
        const std::string ext = lower(toUtf8(p.extension()));
        if (ext != ".mp4" && ext != ".mov" && ext != ".m4v" && ext != ".webm") {
            continue;
        }
        bool ok = false;
        int64_t t = fileTimeSeconds(p, ok);
        result.push_back(VideoFile { toUtf8(p), ok ? t : 0 });
    }

    std::stable_sort(result.begin(), result.end(), [](const VideoFile& a, const VideoFile& b) {
        if (a.modifiedTime != b.modifiedTime) {
            return a.modifiedTime > b.modifiedTime;
        }
        return lower(a.path) < lower(b.path);
    });
    return result;
}
}
