// SPDX-License-Identifier: GPL-3.0-only
// Jubelle engine: the test corpus (jubelle_engine/corpus/README.md).
//
// Public scores come from the repository. Private scores (the user's own, decision D-E17)
// come only from a folder outside the repository: $JUBELLE_PRIVATE_CORPUS, or
// ~/JubellePrivateScores. They are never copied, uploaded or printed: reports name them
// only as "private #n".
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace je::test {
struct CorpusScore {
    std::filesystem::path path;
    std::string displayName;   // the file name for public scores, "private #n" for private ones
    bool isPrivate = false;
};

std::vector<CorpusScore> publicCorpus();
//! empty when the private folder is absent (as in CI)
std::vector<CorpusScore> privateCorpus();
std::filesystem::path privateCorpusFolder();
}
