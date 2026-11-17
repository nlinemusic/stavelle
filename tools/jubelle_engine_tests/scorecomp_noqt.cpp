/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: a Qt-free copy of MuseScore's test helper ScoreComp
 * (src/engraving/tests/utils/scorecomp.cpp), so MuseScore's own
 * save-and-compare tests can run in this tool. Same behaviour: `diff -u
 * --strip-trailing-cr reference saved`, and the test fails if they differ.
 */
#include "engraving/tests/utils/scorecomp.h"
#include "engraving/tests/utils/scorerw.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace mu::engraving;

bool ScoreComp::saveCompareScore(Score* score, const String& saveName, const String& compareWithLocalPath)
{
    if (!ScoreRW::saveScore(score, saveName)) {
        return false;
    }
    return compareFiles(ScoreRW::rootPath() + u"/" + compareWithLocalPath, saveName);
}

bool ScoreComp::saveCompareMimeData(muse::ByteArray mimeData, const String& saveName, const String& compareWithLocalPath)
{
    if (!ScoreRW::saveMimeData(mimeData, saveName)) {
        return false;
    }
    return compareFiles(ScoreRW::rootPath() + u"/" + compareWithLocalPath, saveName);
}

bool ScoreComp::compareFiles(const String& fullPath1, const String& fullPath2)
{
    const std::string cmd = "diff -u --strip-trailing-cr '" + fullPath1.toStdString() + "' '" + fullPath2.toStdString() + "'";
    const int code = std::system(cmd.c_str());
    if (code != 0) {
        std::fprintf(stderr, "%s failed with code: %d\n", cmd.c_str(), code);
        return false;
    }
    return true;
}
