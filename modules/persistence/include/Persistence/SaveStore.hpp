#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace BRITE::Persistence {

/// An application's own files and a game state kept among them, through PhysicsFS.
///
/// While a SaveStore lives, PhysicsFS is initialised: the application's assets are mounted read-only -- `assets.pak`,
/// else `build/assets.pak`, else the loose `assets/` folder (mounted as `/assets`), the first found, relative to the
/// working directory -- and writes go to one folder: the user's preference folder for `orgName`/`appName`, unless
/// another is given. PhysicsFS is one per process, so a process has at most one SaveStore at a time.
///
/// A framework module, built when BRITE_BUILD_PERSISTENCE is on. The Application does none of this itself: an
/// application that wants it owns a SaveStore, for instance beside its Application (GetOrgName, GetAppName).
class SaveStore {
  public:
    /// `writeDirectory` replaces the preference folder, which is otherwise made if it is missing.
    SaveStore(const std::string& orgName, const std::string& appName,
              const std::optional<std::filesystem::path>& writeDirectory = std::nullopt);
    ~SaveStore();

    SaveStore(const SaveStore&) = delete;
    SaveStore& operator=(const SaveStore&) = delete;

    /// Whether PhysicsFS started and has somewhere to write; when it did not, Save and Load refuse, saying why.
    bool CanWrite() const {
        return m_canWrite;
    }

    /// The state SaveState writes and LoadState replaces.
    nlohmann::json& GetGameState() {
        return m_gameState;
    }

    /// Writes the game state, as indented JSON, to `filename` in the write folder. False, logged, if it could not.
    bool SaveState(const std::string& filename);
    /// Replaces the game state with `filename`'s, read from the write folder or any mount. False, logged, and the
    /// state untouched, if there is no such file or it is not JSON.
    bool LoadState(const std::string& filename);

  private:
    bool m_initialised = false;
    bool m_canWrite = false;
    nlohmann::json m_gameState;
};

} // namespace BRITE::Persistence
