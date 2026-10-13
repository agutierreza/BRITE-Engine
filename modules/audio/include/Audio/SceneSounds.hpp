#pragma once

#include <memory>
#include <string>
#include <vector>

namespace SoLoud {
class Wav;
class WavStream;
} // namespace SoLoud

namespace BRITE::Audio {

/// Sounds a scene loads and keeps for as long as it lives -- a SoLoud Wav (decoded into memory) or WavStream (read as
/// it plays) per file, each freed with this. A framework module, built when BRITE_BUILD_AUDIO is on: a scene that
/// wants sound holds one as a member; the framework itself plays nothing and owns no audio device.
class SceneSounds {
  public:
    SceneSounds();
    ~SceneSounds();

    SceneSounds(const SceneSounds&) = delete;
    SceneSounds& operator=(const SceneSounds&) = delete;

    /// Loads `path` whole. The sound is kept, and the pointer valid, until this is destroyed -- also when the file
    /// could not be read, in which case the sound is silent and SoLoud's own load said why.
    SoLoud::Wav* LoadWav(const std::string& path);
    /// The same, streamed from the file as it plays.
    SoLoud::WavStream* LoadWavStream(const std::string& path);

  private:
    std::vector<std::unique_ptr<SoLoud::Wav>> m_wavs;
    std::vector<std::unique_ptr<SoLoud::WavStream>> m_wavStreams;
};

} // namespace BRITE::Audio
