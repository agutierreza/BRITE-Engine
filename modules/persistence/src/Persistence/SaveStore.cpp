#include <Persistence/SaveStore.hpp>

#include <memory>

#include <physfs.h>
#include <spdlog/spdlog.h>

namespace BRITE::Persistence {

SaveStore::SaveStore(const std::string& orgName, const std::string& appName,
                     const std::optional<std::filesystem::path>& writeDirectory) {
    if (!PHYSFS_init(appName.c_str())) {
        spdlog::critical("PHYSFS: Failed to initialize! Error: {}", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return;
    }
    m_initialised = true;

    std::string writeDir;
    if (writeDirectory) {
        writeDir = writeDirectory->string();
    } else if (const char* prefDir = PHYSFS_getPrefDir(orgName.c_str(), appName.c_str())) {
        writeDir = prefDir;
    } else {
        spdlog::error("PHYSFS: Failed to get pref dir for {} / {}", orgName, appName);
    }
    if (!writeDir.empty()) {
        if (PHYSFS_setWriteDir(writeDir.c_str()) == 0) {
            spdlog::error("PHYSFS: Failed to set write dir to {}. Error: {}", writeDir,
                          PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        } else {
            spdlog::info("PHYSFS: Write dir set to {}", writeDir);
            m_canWrite = true;
        }
    }
    // The write folder is searched first, so a state saved there is what LoadState reads back.
    if (m_canWrite)
        PHYSFS_mount(writeDir.c_str(), nullptr, 0);

    if (PHYSFS_mount("assets.pak", NULL, 1)) {
        spdlog::info("PHYSFS: Mounted assets.pak");
    } else if (PHYSFS_mount("build/assets.pak", NULL, 1)) {
        spdlog::info("PHYSFS: Mounted build/assets.pak");
    } else if (PHYSFS_mount("assets", "assets", 1)) {
        spdlog::info("PHYSFS: Mounted local assets folder to /assets");
    } else {
        spdlog::warn("PHYSFS: Failed to mount any assets!");
    }
}

SaveStore::~SaveStore() {
    if (m_initialised)
        PHYSFS_deinit();
}

bool SaveStore::SaveState(const std::string& filename) {
    if (!m_canWrite) {
        spdlog::error("PHYSFS: nowhere to write {}: the store has no write folder", filename);
        return false;
    }
    try {
        std::string serialized = m_gameState.dump(4);

        PHYSFS_File* file = PHYSFS_openWrite(filename.c_str());
        if (!file) {
            spdlog::error("PHYSFS: Failed to open {} for writing. Error: {}", filename,
                          PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            return false;
        }

        PHYSFS_sint64 written = PHYSFS_writeBytes(file, serialized.c_str(), serialized.length());
        PHYSFS_close(file);

        if (written != static_cast<PHYSFS_sint64>(serialized.length())) {
            spdlog::error("PHYSFS: Failed to write all bytes to {}", filename);
            return false;
        }

        spdlog::info("Saved state to {}", filename);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception saving state: {}", e.what());
        return false;
    }
}

bool SaveStore::LoadState(const std::string& filename) {
    if (!m_initialised) {
        spdlog::error("PHYSFS: cannot read {}: PhysicsFS did not start", filename);
        return false;
    }
    if (!PHYSFS_exists(filename.c_str())) {
        spdlog::warn("Save file {} does not exist", filename);
        return false;
    }

    PHYSFS_File* file = PHYSFS_openRead(filename.c_str());
    if (!file) {
        spdlog::error("PHYSFS: Failed to open {} for reading. Error: {}", filename,
                      PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }
    // Closed on every path below, including one that throws while reading or parsing.
    const std::unique_ptr<PHYSFS_File, void (*)(PHYSFS_File*)> fileGuard(file, [](PHYSFS_File* f) {
        PHYSFS_close(f);
    });

    const PHYSFS_sint64 size = PHYSFS_fileLength(file);
    if (size < 0) {
        spdlog::error("PHYSFS: Could not determine the length of {}", filename);
        return false;
    }

    try {
        std::string buffer;
        buffer.resize(static_cast<std::size_t>(size));

        const PHYSFS_sint64 readBytes = PHYSFS_readBytes(file, buffer.data(), size);
        if (readBytes != size) {
            spdlog::error("PHYSFS: Failed to read all bytes from {}", filename);
            return false;
        }

        m_gameState = nlohmann::json::parse(buffer);
        spdlog::info("Loaded state from {}", filename);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception loading state: {}", e.what());
        return false;
    }
}

} // namespace BRITE::Persistence
