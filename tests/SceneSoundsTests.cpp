// The audio module: a scene's sounds, loaded through SoLoud and kept for as long as the scene's SceneSounds lives.
// The sound is a WAV file the test writes itself, so its length is known by hand.

#include <Audio/SceneSounds.hpp>
#include <gtest/gtest.h>

#include <soloud_wav.h>
#include <soloud_wavstream.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using BRITE::Audio::SceneSounds;

namespace {

constexpr std::uint32_t SAMPLE_RATE = 8000;
constexpr std::uint32_t SAMPLES = 800; // a tenth of a second at 8 kHz

void PutU32(std::ofstream& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.put(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void PutU16(std::ofstream& out, std::uint16_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>(v >> 8));
}

/// A mono 16-bit PCM WAV of SAMPLES silent samples at SAMPLE_RATE: the 44-byte canonical header, then the data.
void WriteWav(const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::binary);
    const std::uint32_t dataBytes = SAMPLES * 2;
    out.write("RIFF", 4);
    PutU32(out, 36 + dataBytes);
    out.write("WAVEfmt ", 8);
    PutU32(out, 16);              // the fmt chunk's size
    PutU16(out, 1);               // PCM
    PutU16(out, 1);               // mono
    PutU32(out, SAMPLE_RATE);     // samples a second
    PutU32(out, SAMPLE_RATE * 2); // bytes a second
    PutU16(out, 2);               // bytes a frame
    PutU16(out, 16);              // bits a sample
    out.write("data", 4);
    PutU32(out, dataBytes);
    for (std::uint32_t i = 0; i < SAMPLES; ++i)
        PutU16(out, 0);
}

struct ScratchFolder {
    std::filesystem::path path;
    ScratchFolder() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("brite_scene_sounds_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~ScratchFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

} // namespace

TEST(SceneSounds, AWavIsLoadedWholeOrStreamedAndKeptUntilTheSoundsGo) {
    // BY HAND: 800 samples at 8000 a second last 800 / 8000 = 0.1 s, loaded whole or streamed.
    // MUTATION: LoadWav not loading the file -> length 0, red.
    ScratchFolder folder;
    const std::filesystem::path file = folder.path / "tone.wav";
    WriteWav(file);
    SceneSounds sounds;
    SoLoud::Wav* whole = sounds.LoadWav(file.string());
    SoLoud::WavStream* streamed = sounds.LoadWavStream(file.string());
    ASSERT_NE(whole, nullptr);
    ASSERT_NE(streamed, nullptr);
    EXPECT_NEAR(whole->getLength(), 0.1, 1e-6);
    EXPECT_NEAR(streamed->getLength(), 0.1, 1e-6);
}

TEST(SceneSounds, AFileThatCannotBeReadIsKeptAsASilentSound) {
    // As the scene's loader always did: the pointer is valid, the sound empty, and SoLoud's own load said why.
    SceneSounds sounds;
    SoLoud::Wav* missing = sounds.LoadWav("no such file.wav");
    ASSERT_NE(missing, nullptr);
    EXPECT_EQ(missing->getLength(), 0.0);
}
