// SPDX-License-Identifier: GPL-3.0-only
#include "corpus.h"

#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace je::test {
namespace {
bool isScore(const fs::path& p)
{
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".mscz" || ext == ".mscx" || ext == ".musicxml" || ext == ".mxl";
}

std::vector<fs::path> scoresIn(const fs::path& folder, bool recursive)
{
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return out;
    }
    if (recursive) {
        for (const auto& e : fs::recursive_directory_iterator(folder, ec)) {
            if (e.is_regular_file(ec) && isScore(e.path())) {
                out.push_back(e.path());
            }
        }
    } else {
        for (const auto& e : fs::directory_iterator(folder, ec)) {
            if (e.is_regular_file(ec) && isScore(e.path())) {
                out.push_back(e.path());
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}
}

std::vector<CorpusScore> publicCorpus()
{
    const fs::path repo = fs::u8path(JE_REPO_DIR);
    std::vector<CorpusScore> out;
    for (const fs::path& folder : { repo / "demos", repo / "tools/jubelle_templates/testscores", repo / "tools/jubelle_checks/data" }) {
        for (const fs::path& p : scoresIn(folder, false)) {
            out.push_back({ p, p.filename().u8string(), false });
        }
    }
    return out;
}

fs::path privateCorpusFolder()
{
    if (const char* env = std::getenv("JUBELLE_PRIVATE_CORPUS")) {
        if (*env) {
            return fs::u8path(env);
        }
    }
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        home = std::getenv("USERPROFILE");
    }
    return home ? fs::u8path(home) / "JubellePrivateScores" : fs::path();
}

std::vector<CorpusScore> privateCorpus()
{
    std::vector<CorpusScore> out;
    const fs::path folder = privateCorpusFolder();
    if (folder.empty()) {
        return out;
    }
    // never inside the repository (D-E17): refuse a private folder placed in it
    const fs::path repo = fs::weakly_canonical(fs::u8path(JE_REPO_DIR));
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(folder, ec);
    const std::string c = canon.u8string(), r = repo.u8string();
    if (c.compare(0, r.size(), r) == 0 && canon.u8string().find("jubelle_engine/corpus/private") == std::string::npos) {
        return out;
    }
    int n = 0;
    for (const fs::path& p : scoresIn(folder, true)) {
        out.push_back({ p, "private #" + std::to_string(++n), true });
    }
    return out;
}
}
