// raylib's file loads routed through a content root (Backends/Raylib/RaylibContentLoads.hpp). Every model, glTF buffer,
// image and texture raylib loads is read by LoadFileData, and every text and shader file by LoadFileText, so these two
// are what is checked; no window is needed to call them.

#include "ScratchFolder.hpp"

#include <Backends/Raylib/RaylibContentLoads.hpp>
#include <Content/ContentRoot.hpp>

#include <raylib.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace BRITE::Content;
using BRITE::Raylib::FileLoadRouting;
using BRITE::Raylib::ScopedFileLoadRouting;
using BRITE::Test::ScratchFolder;

namespace {

constexpr const char* SHA256_ABC = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

/// What raylib's LoadFileData returns for `file`, as text, or nothing when it returns no data.
std::optional<std::string> RaylibData(const std::filesystem::path& file) {
    int size = 0;
    unsigned char* data = LoadFileData(file.generic_string().c_str(), &size);
    if (!data)
        return std::nullopt;
    std::string text(reinterpret_cast<const char*>(data), static_cast<std::size_t>(size));
    UnloadFileData(data);
    return text;
}

std::optional<std::string> RaylibText(const std::filesystem::path& file) {
    char* text = LoadFileText(file.generic_string().c_str());
    if (!text)
        return std::nullopt;
    std::string copy(text);
    UnloadFileText(text);
    return copy;
}

struct Fixture {
    ScratchFolder folder{"raylib_content"};
    ScratchFolder elsewhere{"raylib_elsewhere"};
    ContentRoot root;
    std::vector<std::string> outside;

    static Manifest Shipped() {
        Manifest manifest;
        manifest.files = {ManifestEntry{"models/a.txt", 3, *DigestFromHex(SHA256_ABC)},
                          ManifestEntry{"swapped.txt", 3, *DigestFromHex(SHA256_ABC)}};
        return manifest;
    }
    Fixture() : root(std::make_shared<FolderSource>(folder.path), Shipped()) {
        folder.Write("models/a.txt", "abc");
        folder.Write("swapped.txt", "abd");
        folder.Write("extra.txt", "abc");
        elsewhere.Write("plain.txt", "plain");
    }
    FileLoadRouting Routing(bool refuseOutside) {
        FileLoadRouting routing;
        routing.root = &root;
        routing.refuseOutsideRoot = refuseOutside;
        routing.outside = [this](std::string_view path) { outside.emplace_back(path); };
        return routing;
    }
};

} // namespace

TEST(RaylibContentLoads, AFileInsideTheRootIsReadByTheRootByPathOrByName) {
    Fixture fixture;
    ScopedFileLoadRouting routing(fixture.Routing(true));
    EXPECT_EQ(RaylibData(fixture.folder.path / "models" / "a.txt"), std::optional<std::string>("abc"));
    EXPECT_EQ(RaylibData("models/a.txt"), std::optional<std::string>("abc")); // a content name, not the working dir
    EXPECT_EQ(RaylibText(fixture.folder.path / "models" / "a.txt"), std::optional<std::string>("abc"));
    EXPECT_TRUE(fixture.outside.empty());
}

TEST(RaylibContentLoads, AStrictRootRefusesWhatItsManifestLacksOrWhatChanged) {
    Fixture fixture;
    ScopedFileLoadRouting routing(fixture.Routing(true));
    // MUTATION: the callback reading plain files instead of through the root -> both read, red.
    EXPECT_FALSE(RaylibData(fixture.folder.path / "extra.txt").has_value());
    EXPECT_FALSE(RaylibData(fixture.folder.path / "swapped.txt").has_value());
    EXPECT_FALSE(RaylibText(fixture.folder.path / "swapped.txt").has_value());
}

TEST(RaylibContentLoads, AReadOutsideTheRootIsReportedAndRefusedUnlessTheRoutingSaysToReadItPlain) {
    Fixture fixture;
    const std::filesystem::path plain = fixture.elsewhere.path / "plain.txt";
    {
        // MUTATION: an outside path read plain while refuseOutsideRoot holds -> "plain", red.
        ScopedFileLoadRouting routing(fixture.Routing(true));
        EXPECT_FALSE(RaylibData(plain).has_value());
    }
    ASSERT_EQ(fixture.outside.size(), 1u);
    EXPECT_EQ(fixture.outside[0], plain.generic_string());
    {
        ScopedFileLoadRouting routing(fixture.Routing(false));
        EXPECT_EQ(RaylibData(plain), std::optional<std::string>("plain"));
        EXPECT_EQ(RaylibText(plain), std::optional<std::string>("plain"));
    }
    EXPECT_EQ(fixture.outside.size(), 3u);
}

TEST(RaylibContentLoads, WhenTheRoutingEndsRaylibReadsFilesItselfAgain) {
    Fixture fixture;
    {
        ScopedFileLoadRouting routing(fixture.Routing(true));
        ASSERT_FALSE(RaylibData(fixture.folder.path / "extra.txt").has_value());
    }
    // MUTATION: the scope's end not clearing the callbacks -> still refused, red.
    EXPECT_EQ(RaylibData(fixture.folder.path / "extra.txt"), std::optional<std::string>("abc"));
}

TEST(RaylibContentLoads, ARelativeNameRaylibBuildsWithADotFolderIsTheContentNameItNormalisesTo) {
    // raylib looks a model's external texture or material up as "<the model's folder>/<file>", and the folder of a
    // model named with no folder is ".": so "./models/a.txt" is the shipped "models/a.txt", read by the root.
    // MUTATION: the relative name not normalised -> taken for a path outside the root and refused, red.
    Fixture fixture;
    ScopedFileLoadRouting routing(fixture.Routing(true));
    EXPECT_EQ(RaylibData("./models/a.txt"), std::optional<std::string>("abc"));
    EXPECT_EQ(RaylibData("models/./a.txt"), std::optional<std::string>("abc"));
    EXPECT_FALSE(RaylibData("./extra.txt").has_value()); // normalised, and still not in the manifest
    EXPECT_TRUE(fixture.outside.empty());
}

TEST(RaylibContentLoads, AScopeInsideAnotherPutsBackTheOuterRoutingWhenItEnds) {
    // MUTATION: the inner scope's end stopping all routing -> the outer's strict root no longer refuses, red.
    Fixture fixture;
    ScopedFileLoadRouting outer(fixture.Routing(true));
    {
        ScopedFileLoadRouting inner(fixture.Routing(false));
        EXPECT_EQ(BRITE::Raylib::CurrentFileLoadRouting().refuseOutsideRoot, false);
    }
    EXPECT_EQ(BRITE::Raylib::CurrentFileLoadRouting().root, &fixture.root);
    EXPECT_EQ(BRITE::Raylib::CurrentFileLoadRouting().refuseOutsideRoot, true);
    EXPECT_FALSE(RaylibData(fixture.folder.path / "extra.txt").has_value());
}
