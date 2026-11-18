// Jubelle R3: checks release files exactly as Jubelle's updater does, with the updater's own code
// (src/framework/update/internal/jubelle/updateverify.cpp) and the public key built in the same way
// (-DJUBELLE_RELEASE_PUBLIC_KEY="<64 hex digits>").
//
//   verify_release <SHA256SUMS> <SHA256SUMS.sig> <file>...
//
// For each file it prints the verdict, then also checks that a damaged copy and a changed checksum list
// are refused. Exit code 0 only if every file is accepted and every damaged case is refused.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "framework/update/internal/jubelle/updateverify.h"

using namespace muse::update::jubelle;

namespace {
Bytes readAll(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::string baseName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char* text(Verdict v)
{
    switch (v) {
    case Verdict::Ok: return "accepted";
    case Verdict::NoReleaseKey: return "refused: this build has no release key";
    case Verdict::BadSignature: return "refused: the checksum list is not signed with Jubelle's key";
    case Verdict::NotListed: return "refused: not in the checksum list";
    case Verdict::WrongChecksum: return "refused: the file was changed or damaged";
    }
    return "?";
}
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: verify_release <SHA256SUMS> <SHA256SUMS.sig> <file>...\n");
        return 2;
    }
    const Bytes sumsBytes = readAll(argv[1]);
    const std::string sums(sumsBytes.begin(), sumsBytes.end());
    const Bytes sig = readAll(argv[2]);
    const std::string key = releasePublicKeyHex();
    std::printf("release key built in: %s\n", key.size() == 64 ? "yes (64 hex digits)" : "NO");
    if (key.size() != 64) {
        return 1;
    }
    bool ok = true;
    for (int i = 3; i < argc; ++i) {
        const std::string name = baseName(argv[i]);
        Bytes file = readAll(argv[i]);
        const Verdict v = verifyInstaller(file, name, sums, sig, key);
        std::printf("%s (%zu bytes): %s\n", name.c_str(), file.size(), text(v));
        ok = ok && v == Verdict::Ok;

        // the updater must refuse a damaged download...
        if (!file.empty()) {
            file[file.size() / 2] ^= 0x01;
            const Verdict damaged = verifyInstaller(file, name, sums, sig, key);
            std::printf("  one byte changed: %s\n", text(damaged));
            ok = ok && damaged == Verdict::WrongChecksum;
            file[file.size() / 2] ^= 0x01;
        }
        // ...and a checksum list that isn't the signed one
        std::string forged = sums;
        if (!forged.empty()) {
            forged[0] = forged[0] == '0' ? '1' : '0';
            const Verdict bad = verifyInstaller(file, name, forged, sig, key);
            std::printf("  checksum list altered: %s\n", text(bad));
            ok = ok && bad == Verdict::BadSignature;
        }
    }
    std::printf("%s\n", ok ? "RESULT: PASS" : "RESULT: FAIL");
    return ok ? 0 : 1;
}
