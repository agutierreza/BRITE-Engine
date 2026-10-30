// The manifest a BUILD writes (cmake/BriteContentManifest.cmake), run here as brite_content_manifest runs it: the text
// must be exactly what Manifest::ToJson writes for the same files, and a list naming what is not there must stop the
// build rather than write a manifest that lies.

#include "ScratchFolder.hpp"

#include <Content/Manifest.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#if !defined(BRITE_TEST_CMAKE_COMMAND) || !defined(BRITE_TEST_MANIFEST_SCRIPT)
#error "CMakeLists.txt defines BRITE_TEST_CMAKE_COMMAND and BRITE_TEST_MANIFEST_SCRIPT for this file"
#endif

using BRITE::Test::ScratchFolder;

namespace {

constexpr const char* SHA256_EMPTY = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr const char* SHA256_ABC = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

/// Runs the script with `definitions` (each "NAME=value") and returns its exit code.
int RunScript(const std::vector<std::string>& definitions) {
    std::string command = std::string("\"") + BRITE_TEST_CMAKE_COMMAND + "\"";
    for (const std::string& definition : definitions)
        command += " \"-D" + definition + "\"";
    command += std::string(" -P \"") + BRITE_TEST_MANIFEST_SCRIPT + "\"";
#ifdef _WIN32
    // cmd.exe /c drops the first and last quote of a line that starts with one: give it a pair to drop.
    command = "\"" + command + " 2>NUL\"";
#else
    command += " 2>/dev/null";
#endif
    return std::system(command.c_str());
}

struct Build {
    ScratchFolder folder{"content_script"};
    std::filesystem::path output = folder.path / "Tool.manifest.json";
    std::filesystem::path lists = folder.path / "work" / "lists.txt";

    Build() {
        folder.Write("Tool.exe", "abc");
        folder.Write("a.txt", "abc");
        folder.Write("sub/Empty.bin", "");
        folder.Write("work/names.txt", "Tool.exe\nsub/Empty.bin\n");
        folder.Write("work/extra.txt", "a.txt\nsub/Empty.bin\n"); // one name twice across lists: listed once
        folder.Write("work/properties.txt", "key=value\ncolour=blue\n");
        folder.Write("work/version.txt", "abc123\n");
        Lists({"work/names.txt", "work/extra.txt"});
    }
    void Lists(const std::vector<std::string>& relative) const {
        std::string text;
        for (const std::string& list : relative)
            text += (folder.path / list).generic_string() + "\n";
        folder.Write("work/lists.txt", text);
    }
    int Run(const std::string& sequence = "4") const {
        return RunScript({"OUTPUT=" + output.generic_string(), "ROOT=" + folder.path.generic_string(), "APP=Tool",
                          "VERSION_FILE=" + (folder.path / "work/version.txt").generic_string(), "SEQUENCE=" + sequence,
                          "PROPERTIES_FILE=" + (folder.path / "work/properties.txt").generic_string(),
                          "LISTS_FILE=" + lists.generic_string()});
    }
};

} // namespace

TEST(ContentManifestScript, WritesEveryListedFileWithItsSizeAndHashAsToJsonWouldWriteIt) {
    Build build;
    ASSERT_EQ(build.Run(), 0);
    ASSERT_TRUE(std::filesystem::exists(build.output));
    const std::string written = BRITE::Test::ReadText(build.output);

    // BY HAND: the version from the file's first line; properties and files in byte order ('T' < 'a' < 's'); each
    // file's size; the SHA-256 of "abc" and of nothing, as FIPS 180-2 publishes them.
    // MUTATION: the sizes taken from the wrong file, or the hash of the path rather than the file -> red.
    const std::string expected = std::string("{\n"
                                             "  \"app\": \"Tool\",\n"
                                             "  \"version\": \"abc123\",\n"
                                             "  \"sequence\": 4,\n"
                                             "  \"properties\": {\n"
                                             "    \"colour\": \"blue\",\n"
                                             "    \"key\": \"value\"\n"
                                             "  },\n"
                                             "  \"files\": [\n"
                                             "    {\n"
                                             "      \"path\": \"Tool.exe\",\n"
                                             "      \"size\": 3,\n"
                                             "      \"sha256\": \"") +
                                 SHA256_ABC +
                                 "\"\n    },\n    {\n"
                                 "      \"path\": \"a.txt\",\n"
                                 "      \"size\": 3,\n"
                                 "      \"sha256\": \"" +
                                 SHA256_ABC +
                                 "\"\n    },\n    {\n"
                                 "      \"path\": \"sub/Empty.bin\",\n"
                                 "      \"size\": 0,\n"
                                 "      \"sha256\": \"" +
                                 SHA256_EMPTY + "\"\n    }\n  ]\n}\n";
    EXPECT_EQ(written, expected);

    std::string error;
    const std::optional<BRITE::Content::Manifest> parsed = BRITE::Content::Manifest::Parse(written, error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(parsed->ToJson(), written); // the build's text and the module's are one format
}

TEST(ContentManifestScript, AnUnchangedBuildLeavesTheManifestAlone) {
    Build build;
    ASSERT_EQ(build.Run(), 0);
    const auto first = std::filesystem::last_write_time(build.output);
    std::filesystem::last_write_time(build.output, first - std::chrono::hours(1));
    const auto backdated = std::filesystem::last_write_time(build.output);
    ASSERT_EQ(build.Run(), 0);
    EXPECT_EQ(std::filesystem::last_write_time(build.output), backdated);
}

TEST(ContentManifestScript, AListNamingAFileThatIsNotThereStopsTheBuildAndWritesNothing) {
    // MUTATION: a missing file skipped instead of refused -> exit 0 and a manifest without it, red.
    Build build;
    build.folder.Write("work/extra.txt", "a.txt\nnot-built.dll\n");
    EXPECT_NE(build.Run(), 0);
    EXPECT_FALSE(std::filesystem::exists(build.output));
}

TEST(ContentManifestScript, ANameThatIsNoContentNameOrIsListedTwiceIgnoringCaseStopsTheBuild) {
    for (const char* names : {"Tool.exe\n../outside.txt\n", "Tool.exe\nA.TXT\na.txt\n", "Tool.exe\nsub/./a\n"}) {
        Build build;
        build.folder.Write("work/names.txt", names);
        build.Lists({"work/names.txt"});
        EXPECT_NE(build.Run(), 0) << names;
        EXPECT_FALSE(std::filesystem::exists(build.output)) << names;
    }
}

TEST(ContentManifestScript, PropertiesAreWrittenInKeyOrderAsToJsonWritesThem) {
    // BY HAND: the keys "a" and "a2" in byte order are "a", "a2" ("a" is a prefix of "a2"). The lines "a=1" and "a2=2"
    // in byte order are the other way round: '2' (0x32) sorts before '=' (0x3D).
    // MUTATION: the script sorting whole key=value lines -> "a2" written first, red.
    Build build;
    build.folder.Write("work/properties.txt", "a=1\na2=2\n");
    ASSERT_EQ(build.Run(), 0);
    const std::string written = BRITE::Test::ReadText(build.output);
    EXPECT_NE(written.find("  \"properties\": {\n    \"a\": \"1\",\n    \"a2\": \"2\"\n  },\n"), std::string::npos)
        << written;
    std::string error;
    const std::optional<BRITE::Content::Manifest> parsed = BRITE::Content::Manifest::Parse(written, error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(parsed->ToJson(), written);
}

TEST(ContentManifestScript, APropertyGivenTwiceOrHoldingAnAtSignStopsTheBuild) {
    // A key given twice would be written twice, and JSON keeps one of them; an '@' would be substituted by the
    // file(CONFIGURE ... @ONLY) that writes the manifest. Both are refused rather than written wrong.
    // MUTATION: duplicate keys let through -> exit 0, red.
    for (const char* properties : {"a=1\na=2\n", "a=x@y@\n", "a=\n"}) {
        Build build;
        build.folder.Write("work/properties.txt", properties);
        EXPECT_NE(build.Run(), 0) << properties;
        EXPECT_FALSE(std::filesystem::exists(build.output)) << properties;
    }
}

TEST(ContentManifestScript, ASequenceWithALeadingZeroStopsTheBuild) {
    // JSON writes no number with a leading zero: "sequence": 007 is no JSON, and Manifest::Parse refuses it. The
    // script refuses it instead of writing it; "0" alone, and a number that starts with any other digit, are whole.
    // MUTATION: the script's old check, "^[0-9]+$" -> "007" and "00" written, exit 0, red.
    for (const char* sequence : {"007", "00", "01"}) {
        Build build;
        EXPECT_NE(build.Run(sequence), 0) << sequence;
        EXPECT_FALSE(std::filesystem::exists(build.output)) << sequence;
    }
    for (const char* sequence : {"0", "10", "1007"}) {
        Build build;
        ASSERT_EQ(build.Run(sequence), 0) << sequence;
        const std::string written = BRITE::Test::ReadText(build.output);
        EXPECT_NE(written.find(std::string("  \"sequence\": ") + sequence + ",\n"), std::string::npos) << written;
        std::string error;
        EXPECT_TRUE(BRITE::Content::Manifest::Parse(written, error).has_value()) << sequence << ": " << error;
    }
}

TEST(ContentManifestScript, AValueHoldingCmakedefineStopsTheBuild) {
    // file(CONFIGURE), which writes the manifest, turns any line holding "#cmakedefine NAME" or "#cmakedefine01 NAME"
    // into a #define or an "/* #undef NAME */" -- the line's key and quotes with it -- so such a value in a property,
    // a property's key or the version is refused rather than written wrong.
    // MUTATION: the "#cmakedefine" refusal removed -> exit 0 and a manifest that is no JSON, red.
    for (const char* properties : {"a=#cmakedefine NAME\n", "a=x # cmakedefine01 NAME\n", "#cmakedefine NAME=1\n"}) {
        Build build;
        build.folder.Write("work/properties.txt", properties);
        EXPECT_NE(build.Run(), 0) << properties;
        EXPECT_FALSE(std::filesystem::exists(build.output)) << properties;
    }
    Build build;
    build.folder.Write("work/version.txt", "#cmakedefine VERSION\n");
    EXPECT_NE(build.Run(), 0);
    EXPECT_FALSE(std::filesystem::exists(build.output));

    Build hash; // a '#' alone is no directive, and is written
    hash.folder.Write("work/properties.txt", "a=#1 cmake define\n");
    ASSERT_EQ(hash.Run(), 0);
    std::string error;
    const std::optional<BRITE::Content::Manifest> parsed =
        BRITE::Content::Manifest::Parse(BRITE::Test::ReadText(hash.output), error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_NE(BRITE::Test::ReadText(hash.output).find("\"a\": \"#1 cmake define\""), std::string::npos);
}
