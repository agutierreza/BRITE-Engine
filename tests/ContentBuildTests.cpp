// The content module's build half, as a client uses it: CMakeLists.txt stages tests/content/ beside BRITETests as a
// content set (brite_add_content) and writes BRITETests.manifest.json (brite_content_manifest). Read back here through
// a strict root, as an application reads its own.

#include <Content/ContentRoot.hpp>
#include <Content/Folders.hpp>

#include <gtest/gtest.h>

using namespace BRITE::Content;

namespace {

constexpr const char* SHA256_EMPTY = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr const char* SHA256_ABC = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

} // namespace

TEST(ContentBuild, TheBuildStagedTheContentSetAndWroteAManifestNamingItAndTheExecutable) {
    std::string error;
    const ContentRoot root = ContentRoot::FromFolder(ExecutableDirectory(), "BRITETests.manifest.json", &error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(root.IsStrict()) << "no BRITETests.manifest.json beside the tests: build every target, not BRITETests "
                                    "alone";
    const Manifest& manifest = *root.GetManifest();
    EXPECT_EQ(manifest.app, "BRITETests");
    EXPECT_EQ(manifest.version, "test");
    EXPECT_EQ(manifest.sequence, 1u);
    EXPECT_EQ(manifest.properties.at("purpose"), "fixture");

    // BY HAND: tests/content/greeting.txt holds the three bytes "abc" and tests/content/sub/empty.bin nothing; their
    // digests are the ones FIPS 180-2 publishes. Staged under the set's INTO folder, "content".
    // MUTATION: the staging dropping the INTO folder -> the names lack "content/", red.
    const ManifestEntry* greeting = manifest.Find("content/greeting.txt");
    ASSERT_NE(greeting, nullptr);
    EXPECT_EQ(greeting->size, 3u);
    EXPECT_EQ(ToHex(greeting->sha256), SHA256_ABC);
    const ManifestEntry* empty = manifest.Find("content/sub/empty.bin");
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(empty->size, 0u);
    EXPECT_EQ(ToHex(empty->sha256), SHA256_EMPTY);
    // The executable is named too, by its own file name. (Not hash-checked here: building BRITETests alone relinks it
    // without rewriting the manifest.)
    EXPECT_NE(manifest.Find("BRITETests.exe"), nullptr);
    EXPECT_EQ(manifest.files.size(), 3u);

    const std::optional<std::vector<std::uint8_t>> bytes = root.Read("content/greeting.txt");
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(std::string(bytes->begin(), bytes->end()), "abc");
}
