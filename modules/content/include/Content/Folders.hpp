#pragma once

// The two places an application's files live, and they are never the same place:
//
//   * what it SHIPPED, beside its executable (ExecutableDirectory) -- read through a ContentRoot, and replaced
//     whole by an installer or an updater;
//   * what its USER made -- saves, logs, settings -- in a folder of the user's (UserFolder), which nothing that
//     replaces the shipped files ever touches.

#include <filesystem>

namespace BRITE::Content {

/// The folder the running executable is in -- not the working directory, which is wherever the process was started
/// from. Falls back to the working directory only if the platform will not say.
std::filesystem::path ExecutableDirectory();

enum class UserFolderKind {
    Documents, ///< the user's documents: files they may open, keep or send (Windows: Documents; elsewhere ~/Documents)
    LocalData, ///< an application's private data on this machine (Windows: %LOCALAPPDATA%; elsewhere
               ///< $XDG_DATA_HOME, else ~/.local/share)
};

/// The user's folder of that kind, absolute, or empty if the platform cannot say. Not created; the application's own
/// folder inside it is the application's to name and make.
std::filesystem::path UserFolder(UserFolderKind kind);

} // namespace BRITE::Content
