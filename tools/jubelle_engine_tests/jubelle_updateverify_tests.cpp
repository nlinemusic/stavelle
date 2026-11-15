/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

// Jubelle R3: updates are installed only when signed by Jubelle's release key
// and unchanged. Reference values come from Python's "cryptography" package
// (OpenSSL) and RFC 8032; none are computed by the code under test.
#include <gtest/gtest.h>

#include <string>

#include "update/internal/jubelle/updateverify.h"

using namespace muse::update::jubelle;

namespace {
struct Vector {
    const char* key;
    const char* message;
    const char* signature;
};
const Vector VECTORS[] = {
// RFC 8032 7.1 test 1 (empty message)
{ "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "", "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
{ "2fa3c670e1fb2f12fb6b3c04a3014dc627f9fb9ec099840cbe13a878ff73e1dd", "34", "08166a0f3517f014e9855658d85bf68ade5cf096b038620365803446ed1c80ce2a67bcd8077c0d8bafb1c9e86de077c1d0c167e3c87932cd747c673edcd45d07" },
{ "2fa3c670e1fb2f12fb6b3c04a3014dc627f9fb9ec099840cbe13a878ff73e1dd", "60be31", "48db069d6e01c689994496214aba9d362a3843d574d4b2497c3d96748c64a6d6d5789966e5bebc5a61487bad4dca35dd291551ccad0890639049ccb7f5587c04" },
{ "2fa3c670e1fb2f12fb6b3c04a3014dc627f9fb9ec099840cbe13a878ff73e1dd", "201e69fedaa0eee8b9997f5c7c2999fdafe593253cd654af4dfad71427a0aeb3fee9232f8af2211f9ee491c5b10becb5563bfc1e6f93427ecbc8fe2955e5cd8e", "be36331942dd974a3da2858eb01033a11912aa5872f60b761f952ad5fafeba7d98be7aaa517ce45a77a2955e19c2182fb76a593bb288371ab62e31e150fc190b" },
{ "2fa3c670e1fb2f12fb6b3c04a3014dc627f9fb9ec099840cbe13a878ff73e1dd", "46dc8ed4b7c2764d2a5a4d767706f85d8690024ad6bda3401be9c8cbccc935f6cd1f61226ae15338ae1a34004d33ba0d246ac04c81b1baf23e3bf9eef5f79f2b4934af87f5520b69b94b0d982e85bb55b672a872637acd7466fcb60e0e8ff18463b0e4b2ba29703474f064ac68f700f5b02b3dc666f45bdeaa2ccaedcd2b5157410e4dee4af2b34f430a073447de636c0e806c957ba684d6431fb5ead7424d09e15d024c5848f23d1fa6f7361d7f618d1532e70e20e2a6668de7f47e8467e546d53ec8e2a1257bdb", "5b30990acb70b1cecac2a3170e32f87bc05a92e0910f08013f25baab7eff82e4a83200d71c8d8fd8d2e8f6a3ee37bbe12f29aeee2816541589f4b91d0c7d7b08" },
{ "2fa3c670e1fb2f12fb6b3c04a3014dc627f9fb9ec099840cbe13a878ff73e1dd", "256c9b3e4fbb498146ef7030cbf9537252dcceadd764b6a32fbb09adeae109c4a997203975352b878b145c8a42d884cf4cfda72d8e1d5dd92589082d852a7122873ee805add58942167a385286195c679f9c6994e45b8ab1098012070961f37de436ddfdc99d6e75af6547cfb11b42072482dc531c2bc3907c9617eb5e5089e40186baa8a57d119e6fb65d00abc32af38e667f022e872d49cc15c90b999b772b4fc7a6fd4c914a16db4708752b0f1544b835c0e719097dfa8701e9232f21f2812687786976ebfcc327f5931765274ba9829b4406f61ff889326ffa9492edeeee3c669f2bf20894ea27e689c66b6b262e4886b8438f39ba76fef8c90c5101fbe6cf9a48d5b0c0a13da900a6adcb3d64069481be21c9c727b8db8c188f341a924c7f88dfa161bfdb0ecc682919d2e64692f8194157f1d4af90988285cf7a9af7c93d5552266afe70e7aae6da47627c2e59af2ea37abc84670ad3c4d36bc08aad1fff8eb8406e2f8a7fc4cce4dd9f0b4110d9f2fa0025c8efe57f37724f4d37ea2b14004077139b4180df3932249962c6857200059aeb8ea17cf3787e0ed29d1c0b63ffd7298374d9bd74fc11add7b9ca6503952269fd669f6376ee71879737fd5f72f8d51c4ac91b6d0c48d41a1e5ec9e6a0392854a8615eef109fc1bfa9e2563701288f29b3d73f6ac2b69edd2c19f264bee462a5baf20fd27ecf14c011ed201f836320adb98bab1686a28d9801210c7736f3eec580dcfc43fe5d049b4d78a7a3ebb92865c8517ed02111f6a652da3524872b6a31d7ffe4587744d5eb783e96968f89be828565e07e5f7d784e9060a721ca807d7633ed123402f376e5bf1496773d19616326be5be5850336b36f13bcae48166882136805a7d1be5e9f276810fdf720d033ca4f2e53cb8ad1919dd51a9fb6d4d509ba64c8cf6803de50d83a2ecfbaeb5342071a48cb2dbd574ab29152572237c4fb659a4016f7a11bc62c5271cf64f25d6f15cc50c4b73f4c7e621513a53cc7e99cd79d7fd9c7bce4e05b0b01faee78e4ea5bf2cc362241b7dcbb2ee21414422aa0281bc1450d21386343fb93547121b38151a58ce94982f56a8679a3be12655dce528ea7c056873a18b8e73581c9be87c0bc4ab8a929e2755a1897819ea00011714c94ddd5ba1843fa74170b1b01b59b36b672d39a4468bbf35144077c4ce631204a8acd87051cb3e3fc7f5400161f0ccf5f79511d35066448d366d4599e209918f403c0dfee29e75973358576133fab861a88df87976f2b075685786751a762c7a87ac2f0f1030ddf779d6cc827574a100d393652b0480e0f154615221721ba6621c4367e69683911112c93f43343326896a3acd8850ab3839018bca4f3930fd30fdf32b1f0186e2e9357df0067931b02b2fb30fb", "6bbd00cb7a66f0e40994f083a833ef2975b2c97db487af19222d031eca7c77cae706f85f4fd2b80f8d785817b64040d70bf7d31b8a95762f3698236f8b8e8000" },
};

Bytes hex(const std::string& s)
{
    Bytes b;
    EXPECT_TRUE(fromHex(s, b));
    return b;
}

Bytes text(const std::string& s)
{
    return Bytes(s.begin(), s.end());
}

const std::string TEST_KEY = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
const std::string TEST_SUMS = "8a96960b548d1d7d5e19719f5b53b2a76b5ab76528dd50a1dc98f76b8d1c7bf0  Jubelle-4.7.7.261116010-x86_64.msi\n1ea50a8caf6085042452db60307ec62662e096120714ddac4fbc471c9456b411  Jubelle-4.7.7.261116010-x86_64.pdb.zip\n";
const std::string TEST_SIG = "b6d7ea0e5f4821d3744afdb9c4099393d4206b00d5247721d9fa8104814fff57d71932190816c89348ba3904fff2b0690b2c6b971e5f33005e06bbd05e883204";
const Bytes TEST_INSTALLER = [] {
    std::string s;
    for (int i = 0; i < 10; ++i) {
        s += "MSI installer bytes for the test";
    }
    return Bytes(s.begin(), s.end());
}();
const std::string INSTALLER_NAME = "Jubelle-4.7.7.261116010-x86_64.msi";
}

TEST(Jubelle_UpdateVerify, Sha256AndSha512MatchTheStandard)
{
    const std::string abc = "abc";
    const auto a = reinterpret_cast<const uint8_t*>(abc.data());
    EXPECT_EQ(toHex(sha256(a, 3).data(), 32), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(toHex(sha512(a, 3).data(), 64),
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    const std::string many(1000, 'a');   // more than one block
    const auto m = reinterpret_cast<const uint8_t*>(many.data());
    EXPECT_EQ(toHex(sha256(m, many.size()).data(), 32), "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    EXPECT_EQ(toHex(sha512(m, many.size()).data(), 64),
              "67ba5535a46e3f86dbfbed8cbbaf0125c76ed549ff8b0b9e03e0c88cf90fa634fa7b12b47d77b694de488ace8d9a65967dc96df599727d3292a8d9d447709c97");
}

TEST(Jubelle_UpdateVerify, Ed25519AcceptsGoodSignaturesOnly)
{
    for (const Vector& v : VECTORS) {
        const Bytes key = hex(v.key), msg = hex(v.message), sig = hex(v.signature);
        SCOPED_TRACE(msg.size());
        ASSERT_EQ(key.size(), 32u);
        ASSERT_EQ(sig.size(), 64u);
        EXPECT_TRUE(ed25519Verify(sig.data(), msg.data(), msg.size(), key.data()));

        // any change anywhere is refused
        Bytes badSig = sig;
        badSig[5] ^= 1;
        EXPECT_FALSE(ed25519Verify(badSig.data(), msg.data(), msg.size(), key.data()));
        badSig = sig;
        badSig[40] ^= 0x10;
        EXPECT_FALSE(ed25519Verify(badSig.data(), msg.data(), msg.size(), key.data()));
        Bytes badKey = key;
        badKey[0] ^= 1;
        EXPECT_FALSE(ed25519Verify(sig.data(), msg.data(), msg.size(), badKey.data()));
        if (!msg.empty()) {
            Bytes badMsg = msg;
            badMsg.back() ^= 0x80;
            EXPECT_FALSE(ed25519Verify(sig.data(), badMsg.data(), badMsg.size(), key.data()));
        }
        Bytes longer = msg;
        longer.push_back(0);
        EXPECT_FALSE(ed25519Verify(sig.data(), longer.data(), longer.size(), key.data()));
    }
}

TEST(Jubelle_UpdateVerify, OnlyASignedUnchangedInstallerPasses)
{
    EXPECT_EQ(verifyInstaller(TEST_INSTALLER, INSTALLER_NAME, TEST_SUMS, text(TEST_SIG), TEST_KEY), Verdict::Ok);
    EXPECT_EQ(verifyInstaller(TEST_INSTALLER, INSTALLER_NAME, TEST_SUMS, hex(TEST_SIG), TEST_KEY), Verdict::Ok);   // raw form too

    // a tampered installer
    Bytes tampered = TEST_INSTALLER;
    tampered[10] ^= 1;
    EXPECT_EQ(verifyInstaller(tampered, INSTALLER_NAME, TEST_SUMS, text(TEST_SIG), TEST_KEY), Verdict::WrongChecksum);

    // a checksum list changed to match a different installer: the signature no longer fits
    std::string forged = TEST_SUMS;
    forged[0] = forged[0] == '0' ? '1' : '0';
    EXPECT_EQ(verifyInstaller(TEST_INSTALLER, INSTALLER_NAME, forged, text(TEST_SIG), TEST_KEY), Verdict::BadSignature);

    // signed by someone else's key
    EXPECT_EQ(verifyInstaller(TEST_INSTALLER, INSTALLER_NAME, TEST_SUMS, text(TEST_SIG), VECTORS[0].key), Verdict::BadSignature);

    // an installer the list doesn't mention
    EXPECT_EQ(verifyInstaller(TEST_INSTALLER, "Other.msi", TEST_SUMS, text(TEST_SIG), TEST_KEY), Verdict::NotListed);

    // damaged signature file, no key in this build
    EXPECT_EQ(verifyInstaller(TEST_INSTALLER, INSTALLER_NAME, TEST_SUMS, text("not a signature"), TEST_KEY), Verdict::BadSignature);
    EXPECT_EQ(verifyInstaller(TEST_INSTALLER, INSTALLER_NAME, TEST_SUMS, text(TEST_SIG), ""), Verdict::NoReleaseKey);
}

TEST(Jubelle_UpdateVerify, ChecksumListAndVersions)
{
    const auto sums = parseSha256Sums(TEST_SUMS + "garbage line\n" + std::string(64, 'A') + " *Binary.zip\r\n");
    EXPECT_EQ(sums.size(), 3u);
    EXPECT_EQ(sums.at("Binary.zip"), std::string(64, 'a'));
    EXPECT_TRUE(sums.count(INSTALLER_NAME));

    EXPECT_GT(compareVersions("4.7.7.261116010", "4.7.7.261115091"), 0);
    EXPECT_LT(compareVersions("v4.7.7.261115091", "4.7.7.261116010"), 0);
    EXPECT_EQ(compareVersions("v4.7.7", "4.7.7.0"), 0);
    EXPECT_GT(compareVersions("4.8", "4.7.9.999999999"), 0);
    EXPECT_EQ(compareVersions("4.7.7-beta", "4.7.7"), 0);   // suffixes don't count; releases are numbered
}
