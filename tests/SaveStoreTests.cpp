// The persistence module: a game state saved through PhysicsFS and read back, in a write folder of the test's own.

#include <Persistence/SaveStore.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using BRITE::Persistence::SaveStore;

namespace {

/// A folder of this test's own under the system's temporary folder, removed with this.
struct ScratchFolder {
    std::filesystem::path path;
    ScratchFolder() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("brite_save_store_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~ScratchFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string ReadFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST(SaveStore, AStateSavedIsWrittenAsJsonInTheWriteFolderAndReadBackByTheNextStore) {
    // Two stores, one after the other (PhysicsFS is one per process): the first saves, the second loads.
    // MUTATION: the write folder not searched by LoadState -> "does not exist", red. SaveState writing nothing ->
    // no file, red.
    ScratchFolder folder;
    {
        SaveStore store("BRITE", "SaveStoreTests", folder.path);
        ASSERT_TRUE(store.CanWrite());
        store.GetGameState()["laps"] = 3;
        store.GetGameState()["name"] = "first";
        ASSERT_TRUE(store.SaveState("state.json"));
    }
    const std::filesystem::path written = folder.path / "state.json";
    ASSERT_TRUE(std::filesystem::is_regular_file(written));
    // BY HAND: dump(4) of {"laps":3,"name":"first"}, keys in nlohmann::json's sorted order, four spaces of indent.
    EXPECT_EQ(ReadFile(written), "{\n    \"laps\": 3,\n    \"name\": \"first\"\n}");

    SaveStore again("BRITE", "SaveStoreTests", folder.path);
    ASSERT_TRUE(again.LoadState("state.json"));
    EXPECT_EQ(again.GetGameState().at("laps"), 3);
    EXPECT_EQ(again.GetGameState().at("name"), "first");
}

TEST(SaveStore, AMissingFileOrOneThatIsNotJsonLeavesTheStateAsItWas) {
    // MUTATION: the state cleared before parsing -> the kept value gone, red.
    ScratchFolder folder;
    {
        std::ofstream(folder.path / "broken.json", std::ios::binary) << "this is not json";
    }
    SaveStore store("BRITE", "SaveStoreTests", folder.path);
    store.GetGameState()["kept"] = true;
    EXPECT_FALSE(store.LoadState("missing.json"));
    EXPECT_FALSE(store.LoadState("broken.json"));
    EXPECT_EQ(store.GetGameState().at("kept"), true);
}
