// The content module: SHA-256, content names, the manifest, a content root over a folder, and the two folders an
// application's files live in.

#include "ScratchFolder.hpp"

#include <Content/ContentRoot.hpp>
#include <Content/Folders.hpp>
#include <Content/Manifest.hpp>
#include <Content/Sha256.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

using namespace BRITE::Content;
using BRITE::Test::ScratchFolder;

namespace {

// FIPS 180-2, Appendix B (SHA-256 examples) and the NIST CAVS short-message vectors: digests published with the
// standard, not computed here.
constexpr const char* SHA256_EMPTY = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr const char* SHA256_ABC = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr const char* SHA256_448_BITS = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
constexpr const char* SHA256_MILLION_A = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

Sha256Digest Digest(const char* hex) {
    const std::optional<Sha256Digest> digest = DigestFromHex(hex);
    EXPECT_TRUE(digest.has_value()) << hex;
    return digest.value_or(Sha256Digest{});
}

ManifestEntry Entry(const char* path, std::uint64_t size, const char* hex) {
    return ManifestEntry{path, size, Digest(hex)};
}

std::string Text(const std::vector<std::uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

} // namespace

TEST(ContentSha256, MatchesTheStandardsPublishedDigests) {
    // MUTATION: a round constant changed, the length written little-endian, or the padding's extra block skipped (the
    // 56-byte message needs one) -> a digest differs, red.
    EXPECT_EQ(ToHex(Sha256Of(std::string_view(""))), SHA256_EMPTY);
    EXPECT_EQ(ToHex(Sha256Of(std::string_view("abc"))), SHA256_ABC);
    EXPECT_EQ(ToHex(Sha256Of(std::string_view("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
              SHA256_448_BITS);
    EXPECT_EQ(ToHex(Sha256Of(std::string(1000000, 'a'))), SHA256_MILLION_A);
}

TEST(ContentSha256, ADigestFedInPiecesIsTheDigestFedWhole) {
    // Pieces that cross the 64-byte block boundary at every offset: the buffering must not lose or repeat a byte.
    const std::string message = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for (std::size_t split = 0; split <= message.size(); ++split) {
        Sha256 hash;
        hash.Update(std::string_view(message).substr(0, split));
        hash.Update(std::string_view(message).substr(split));
        EXPECT_EQ(ToHex(hash.Finish()), SHA256_448_BITS) << "split at " << split;
    }
}

TEST(ContentSha256, HexReadsEitherCaseAndRefusesAnythingElse) {
    EXPECT_EQ(ToHex(Digest("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD")), SHA256_ABC);
    EXPECT_FALSE(DigestFromHex("ba7816bf").has_value());                     // too short
    EXPECT_FALSE(DigestFromHex(std::string(SHA256_ABC) + "00").has_value()); // too long
    EXPECT_FALSE(DigestFromHex(std::string(63, 'a') + "g").has_value());     // not hex
}

TEST(ContentName, IsRelativeSlashSeparatedAndCannotLeaveItsFolder) {
    for (const char* good : {"a", "models/a.glb", "Game.exe", "libc++.dll", "lib-2.0_x.dll", "a/b/c.d"}) {
        EXPECT_TRUE(IsContentName(good)) << good;
    }
    // MUTATION: ".." accepted -> a name reaches the parent folder, red. Uppercase refused -> "Game.exe" red.
    for (const char* bad : {"", "/a", "a/", "a//b", ".", "..", "../a", "a/../b", "./a", "a/.", "a.", "dir./b", "C:/a",
                            "a\\b", "a b", "a:b", "\\\\server\\share", "caf\xc3\xa9"}) {
        EXPECT_FALSE(IsContentName(bad)) << bad;
    }
}

TEST(ContentManifest, ParsesWhatItWritesAndWritesTheSameBytesForTheSameManifest) {
    Manifest manifest;
    manifest.app = "Game";
    manifest.version = "1a2b3c";
    manifest.sequence = 7;
    manifest.properties = {{"key", "value"}, {"colour", "blue"}};
    manifest.files = {Entry("z.txt", 3, SHA256_ABC), Entry("a/Empty.bin", 0, SHA256_EMPTY)};

    // BY HAND: keys in the documented order, properties and files in byte order, two spaces of indent.
    const std::string expected = std::string("{\n"
                                             "  \"app\": \"Game\",\n"
                                             "  \"version\": \"1a2b3c\",\n"
                                             "  \"sequence\": 7,\n"
                                             "  \"properties\": {\n"
                                             "    \"colour\": \"blue\",\n"
                                             "    \"key\": \"value\"\n"
                                             "  },\n"
                                             "  \"files\": [\n"
                                             "    {\n"
                                             "      \"path\": \"a/Empty.bin\",\n"
                                             "      \"size\": 0,\n"
                                             "      \"sha256\": \"") +
                                 SHA256_EMPTY +
                                 "\"\n"
                                 "    },\n"
                                 "    {\n"
                                 "      \"path\": \"z.txt\",\n"
                                 "      \"size\": 3,\n"
                                 "      \"sha256\": \"" +
                                 SHA256_ABC +
                                 "\"\n"
                                 "    }\n"
                                 "  ]\n"
                                 "}\n";
    EXPECT_EQ(manifest.ToJson(), expected);

    std::string error;
    const std::optional<Manifest> parsed = Manifest::Parse(expected, error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(parsed->app, "Game");
    EXPECT_EQ(parsed->version, "1a2b3c");
    EXPECT_EQ(parsed->sequence, 7u);
    EXPECT_EQ(parsed->properties.at("key"), "value");
    ASSERT_EQ(parsed->files.size(), 2u);
    EXPECT_EQ(parsed->files[0].path, "a/Empty.bin"); // sorted on parse too
    EXPECT_EQ(parsed->files[1].size, 3u);
    EXPECT_EQ(ToHex(parsed->files[1].sha256), SHA256_ABC);
    EXPECT_EQ(parsed->ToJson(), expected);
}

TEST(ContentManifest, RefusesEveryMalformedManifestAndSaysWhy) {
    const std::string hash = std::string("\"") + SHA256_ABC + "\"";
    const auto with = [&](const std::string& files) {
        return "{\"app\":\"a\",\"version\":\"v\",\"sequence\":1,\"properties\":{},\"files\":[" + files + "]}";
    };
    const std::vector<std::string> bad = {
        "not json",
        "[]",
        "{\"version\":\"v\",\"sequence\":1,\"properties\":{},\"files\":[]}",                // no app
        "{\"app\":\"a\",\"version\":\"v\",\"sequence\":-1,\"properties\":{},\"files\":[]}", // negative sequence
        "{\"app\":\"a\",\"version\":\"v\",\"sequence\":1,\"properties\":{\"k\":1},\"files\":[]}",
        with("{\"path\":\"../x\",\"size\":3,\"sha256\":" + hash + "}"), // escapes its folder
        with("{\"path\":\"x\",\"size\":3.5,\"sha256\":" + hash + "}"),  // not a whole number
        with("{\"path\":\"x\",\"size\":3,\"sha256\":\"abc\"}"),         // not a digest
        with("{\"path\":\"x\",\"size\":3}"),                            // no digest
        // MUTATION: case-blind duplicates accepted -> two entries for one file on Windows, red.
        with("{\"path\":\"Read.me\",\"size\":3,\"sha256\":" + hash +
             "},{\"path\":\"read.ME\",\"size\":3,\"sha256\":" + hash + "}"),
    };
    for (const std::string& json : bad) {
        std::string error;
        EXPECT_FALSE(Manifest::Parse(json, error).has_value()) << json;
        EXPECT_FALSE(error.empty()) << json;
    }
}

TEST(ContentManifest, FindIgnoresCaseAndChangedListsWhatAnUpdaterMustFetch) {
    Manifest from;
    from.files = {Entry("same.txt", 3, SHA256_ABC), Entry("edited.txt", 3, SHA256_ABC),
                  Entry("resized.txt", 3, SHA256_ABC), Entry("dropped.txt", 3, SHA256_ABC)};
    Manifest to;
    to.files = {Entry("SAME.txt", 3, SHA256_ABC), Entry("edited.txt", 3, SHA256_448_BITS),
                Entry("resized.txt", 4, SHA256_ABC), Entry("added.txt", 0, SHA256_EMPTY)};

    ASSERT_NE(from.Find("Same.TXT"), nullptr);
    EXPECT_EQ(from.Find("Same.TXT")->path, "same.txt");
    EXPECT_EQ(from.Find("missing.txt"), nullptr);

    // BY HAND: same.txt matches (case aside); edited.txt's hash differs; resized.txt's size differs; added.txt is
    // new. dropped.txt is in `from` only, and an updater has nothing to fetch for it.
    // MUTATION: the hash not compared -> edited.txt missing, red. The size not compared -> resized.txt missing, red.
    const std::vector<ManifestEntry> changed = Changed(from, to);
    std::vector<std::string> paths;
    for (const ManifestEntry& entry : changed)
        paths.push_back(entry.path);
    EXPECT_EQ(paths, (std::vector<std::string>{"edited.txt", "resized.txt", "added.txt"}));
}

TEST(ContentRoot, WithoutAManifestServesEveryWellFormedNameInItsFolder) {
    ScratchFolder folder("content_loose");
    folder.Write("models/a.txt", "abc");
    const ContentRoot root(std::make_shared<FolderSource>(folder.path));
    EXPECT_FALSE(root.IsStrict());

    const std::optional<std::vector<std::uint8_t>> bytes = root.Read("models/a.txt");
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(Text(*bytes), "abc");
    ASSERT_TRUE(root.Resolve("models/a.txt").has_value());
    EXPECT_EQ(BRITE::Test::ReadText(*root.Resolve("models/a.txt")), "abc");

    EXPECT_FALSE(root.Read("models/missing.txt").has_value());
    EXPECT_FALSE(root.Resolve("../outside.txt").has_value());
}

TEST(ContentRoot, StrictRefusesWhatTheManifestLacksAndAnyFileThatChangedSince) {
    ScratchFolder folder("content_strict");
    folder.Write("shipped.txt", "abc"); // as shipped
    folder.Write("swapped.txt", "abd"); // same size as shipped, other bytes
    folder.Write("grown.txt", "abcd");  // another size
    folder.Write("extra.txt", "abc");   // not in the manifest
    Manifest manifest;
    manifest.files = {Entry("shipped.txt", 3, SHA256_ABC), Entry("swapped.txt", 3, SHA256_ABC),
                      Entry("grown.txt", 3, SHA256_ABC), Entry("gone.txt", 3, SHA256_ABC)};

    ContentRoot root(std::make_shared<FolderSource>(folder.path), manifest);
    ASSERT_TRUE(root.IsStrict());
#ifdef _WIN32
    EXPECT_TRUE(root.Read("SHIPPED.TXT").has_value()); // Windows' names ignore case, and so does the manifest
#endif
    std::vector<ContentAccess> seen;
    root.SetTap([&](const ContentAccess& access) { seen.push_back(access); });

    EXPECT_TRUE(root.Read("shipped.txt").has_value());
    // MUTATION: strict not consulted -> extra.txt served, red.
    EXPECT_FALSE(root.Read("extra.txt").has_value());
    EXPECT_FALSE(root.Resolve("extra.txt").has_value());
    // MUTATION: the hash not compared on Read -> swapped.txt served, red.
    std::string error;
    EXPECT_FALSE(root.Read("swapped.txt", &error).has_value());
    EXPECT_NE(error.find("SHA-256"), std::string::npos) << error;
    // Resolve stats the file and does not read it: a same-size swap passes Resolve, which is why a reader that can
    // take bytes should Read. The size is checked.
    EXPECT_TRUE(root.Resolve("swapped.txt").has_value());
    // MUTATION: the size not compared -> grown.txt resolved, red.
    EXPECT_FALSE(root.Resolve("grown.txt").has_value());
    EXPECT_FALSE(root.Read("grown.txt").has_value());
    EXPECT_FALSE(root.Read("gone.txt").has_value());

    // The tap saw every access, in order, each refusal with its reason.
    ASSERT_EQ(seen.size(), 8u);
    EXPECT_EQ(seen[0].kind, ContentAccess::Kind::Read);
    EXPECT_EQ(seen[1].kind, ContentAccess::Kind::Refused);
    EXPECT_EQ(seen[1].name, "extra.txt");
    EXPECT_EQ(seen[1].reason, "not in the manifest");
    EXPECT_EQ(seen[4].kind, ContentAccess::Kind::Resolved);
    EXPECT_EQ(seen[7].reason, "in the manifest but missing");
}

TEST(ContentRoot, VerifyNamesEveryFileMissingOrDifferent) {
    ScratchFolder folder("content_verify");
    folder.Write("shipped.txt", "abc");
    folder.Write("swapped.txt", "abd");
    folder.Write("grown.txt", "abcd");
    Manifest manifest;
    manifest.files = {Entry("shipped.txt", 3, SHA256_ABC), Entry("swapped.txt", 3, SHA256_ABC),
                      Entry("grown.txt", 3, SHA256_ABC), Entry("gone.txt", 3, SHA256_ABC)};
    const ContentRoot root(std::make_shared<FolderSource>(folder.path), manifest);

    // By size: grown.txt and gone.txt. By hash: swapped.txt as well.
    // MUTATION: Hash depth not reading the files -> swapped.txt missing from the deep report, red.
    EXPECT_EQ(root.Verify(VerifyDepth::Size).problems.size(), 2u);
    const VerifyReport deep = root.Verify(VerifyDepth::Hash);
    ASSERT_EQ(deep.problems.size(), 3u);
    EXPECT_EQ(deep.problems[0], "swapped.txt: its SHA-256 differs from the manifest's");
    EXPECT_EQ(deep.problems[1], "grown.txt: size 4, the manifest says 3");
    EXPECT_EQ(deep.problems[2], "gone.txt: missing");

    const ContentRoot loose(std::make_shared<FolderSource>(folder.path));
    EXPECT_TRUE(loose.Verify(VerifyDepth::Hash).Clean());
}

TEST(ContentRoot, FromFolderReadsItsManifestAndABrokenOneRefusesEverything) {
    ScratchFolder folder("content_from_folder");
    folder.Write("a.txt", "abc");
    Manifest manifest;
    manifest.files = {Entry("a.txt", 3, SHA256_ABC)};
    folder.Write("App.manifest.json", manifest.ToJson());

    std::string error;
    const ContentRoot root = ContentRoot::FromFolder(folder.path, "App.manifest.json", &error);
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_TRUE(root.IsStrict());
    EXPECT_TRUE(root.Read("a.txt").has_value());

    const ContentRoot none = ContentRoot::FromFolder(folder.path, "Other.manifest.json", &error);
    EXPECT_FALSE(none.IsStrict());

    // MUTATION: a manifest that cannot be parsed ignored -> the root serves everything, red.
    folder.Write("App.manifest.json", "{ broken");
    const ContentRoot broken = ContentRoot::FromFolder(folder.path, "App.manifest.json", &error);
    EXPECT_TRUE(broken.IsStrict());
    EXPECT_FALSE(broken.Read("a.txt").has_value());
    EXPECT_NE(error.find("App.manifest.json"), std::string::npos) << error;
}

TEST(ContentRoot, FromFolderRefusesEverythingWhenItCannotTellWhetherTheManifestIsThere) {
    // A check that fails says neither "there" nor "not there", so the root is not left loose. A name of 40,000
    // characters makes the check itself fail on every platform -- Windows' paths stop at 32,767 characters, POSIX's
    // at PATH_MAX -- where a merely missing manifest, or one under a file, reads plainly as "not there".
    // MUTATION: an error from the check read as "no manifest" -> a loose root that serves a.txt, red.
    ScratchFolder folder("content_from_folder_unchecked");
    folder.Write("a.txt", "abc");
    std::string error;
    const ContentRoot unchecked = ContentRoot::FromFolder(folder.path, std::string(40000, 'm'), &error);
    EXPECT_TRUE(unchecked.IsStrict());
    EXPECT_FALSE(unchecked.Read("a.txt").has_value());
    EXPECT_NE(error.find("could not be checked"), std::string::npos)
        << error.substr(error.size() > 200 ? error.size() - 200 : 0);
}

TEST(ContentRoot, NameOfMapsAPathInsideTheFolderBackToItsNameAndNothingElse) {
    ScratchFolder folder("content_name_of");
    folder.Write("models/a.glb", "abc");
    const ContentRoot root(std::make_shared<FolderSource>(folder.path));

    EXPECT_EQ(root.NameOf(folder.path / "models" / "a.glb"), std::optional<std::string>("models/a.glb"));
    EXPECT_EQ(root.NameOf(folder.path / "models" / ".." / "models" / "a.glb"),
              std::optional<std::string>("models/a.glb"));
    EXPECT_FALSE(root.NameOf(folder.path / ".." / "elsewhere.txt").has_value());
    EXPECT_FALSE(root.NameOf(folder.path).has_value());
    // A sibling whose name merely starts with the folder's is outside it.
    EXPECT_FALSE(root.NameOf(std::filesystem::path(folder.path.string() + "x") / "a.glb").has_value());
}

TEST(ContentRoot, NameOfWorksInAFolderTheCodePageCannotSpell) {
    // A user's folder may hold any character. U+03A9 (Greek capital omega) is in no Western code page, so narrowing the
    // path to it -- what std::filesystem::path::string() does on Windows -- throws; the root compares UTF-8 instead.
    // MUTATION: NameOf back on generic_string() -> std::system_error out of NameOf, red (on Windows).
    ScratchFolder scratch("content_wide");
    const std::filesystem::path folder = scratch.path / std::filesystem::path(u8"\u03a9mega");
    std::filesystem::create_directories(folder / "models");
    {
        std::ofstream(folder / "models" / "a.glb", std::ios::binary) << "abc";
    }
    const ContentRoot root(std::make_shared<FolderSource>(folder));
    std::optional<std::string> name;
    EXPECT_NO_THROW(name = root.NameOf(folder / "models" / "a.glb"));
    EXPECT_EQ(name, std::optional<std::string>("models/a.glb"));
    EXPECT_TRUE(root.Read("models/a.glb").has_value());
}

TEST(ContentFolders, TheExecutablesFolderAndTheUsersFoldersAreThreeAbsolutePlaces) {
    const std::filesystem::path executable = ExecutableDirectory();
    EXPECT_TRUE(executable.is_absolute());
    std::error_code ec;
    EXPECT_TRUE(std::filesystem::is_directory(executable, ec)) << executable;

    const std::filesystem::path documents = UserFolder(UserFolderKind::Documents);
    const std::filesystem::path local = UserFolder(UserFolderKind::LocalData);
    ASSERT_FALSE(documents.empty());
    ASSERT_FALSE(local.empty());
    EXPECT_TRUE(documents.is_absolute());
    EXPECT_TRUE(local.is_absolute());
    EXPECT_NE(documents, local);
    EXPECT_NE(documents, executable);
}
