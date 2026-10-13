// The raylib module's scene assets: textures a scene loads straight through raylib, kept until its RaylibSceneAssets
// goes. raylib needs a window and a GL context to load a texture, so the case opens a hidden one.

#include <Backends/Raylib/RaylibSceneAssets.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

using BRITE::Backends::Raylib::RaylibSceneAssets;

TEST(RaylibSceneAssets, ATextureIsLoadedFromItsFileAndKeptAtItsSize) {
    // BY HAND: a 4 x 2 image written as a PNG reads back as a 4 x 2 texture with a GL name, which is never 0.
    // MUTATION: LoadTexture not calling raylib -> id 0, red.
    ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
    ::InitWindow(16, 16, "BRITE scene assets tests");
    ASSERT_TRUE(::IsWindowReady());

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / ("brite_scene_assets_" + std::to_string(stamp) + ".png");
    Image image = ::GenImageColor(4, 2, RED);
    ASSERT_TRUE(::ExportImage(image, file.string().c_str()));
    ::UnloadImage(image);
    {
        RaylibSceneAssets assets;
        const Texture2D texture = assets.LoadTexture(file.string());
        EXPECT_NE(texture.id, 0u);
        EXPECT_EQ(texture.width, 4);
        EXPECT_EQ(texture.height, 2);
    }
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    ::CloseWindow();
}
