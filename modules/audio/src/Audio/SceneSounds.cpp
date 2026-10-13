#include <Audio/SceneSounds.hpp>

#include <soloud_wav.h>
#include <soloud_wavstream.h>

namespace BRITE::Audio {

SceneSounds::SceneSounds() = default;
SceneSounds::~SceneSounds() = default;

SoLoud::Wav* SceneSounds::LoadWav(const std::string& path) {
    auto wav = std::make_unique<SoLoud::Wav>();
    wav->load(path.c_str());
    SoLoud::Wav* ptr = wav.get();
    m_wavs.push_back(std::move(wav));
    return ptr;
}

SoLoud::WavStream* SceneSounds::LoadWavStream(const std::string& path) {
    auto wav = std::make_unique<SoLoud::WavStream>();
    wav->load(path.c_str());
    SoLoud::WavStream* ptr = wav.get();
    m_wavStreams.push_back(std::move(wav));
    return ptr;
}

} // namespace BRITE::Audio
