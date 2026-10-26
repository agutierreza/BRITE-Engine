#include <Content/Folders.hpp>

#include <cstdlib>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h, which it needs.
#include <knownfolders.h>
#include <shlobj.h>
#endif

namespace BRITE::Content {

std::filesystem::path ExecutableDirectory() {
    std::error_code ec;
#ifdef _WIN32
    // GetModuleFileNameW truncates silently when the buffer is short, and says so only by filling it exactly: grow
    // until it does not.
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0)
            break;
        if (written < buffer.size())
            return std::filesystem::path(std::wstring(buffer.data(), written)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
#else
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec)
        return self.parent_path();
#endif
    return std::filesystem::current_path(ec);
}

std::filesystem::path UserFolder(UserFolderKind kind) {
#ifdef _WIN32
    PWSTR folder = nullptr;
    std::filesystem::path found;
    const KNOWNFOLDERID& id = kind == UserFolderKind::Documents ? FOLDERID_Documents : FOLDERID_LocalAppData;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &folder)))
        found = folder;
    CoTaskMemFree(folder);
    return found;
#else
    const char* home = std::getenv("HOME");
    if (kind == UserFolderKind::LocalData) {
        const char* dataHome = std::getenv("XDG_DATA_HOME");
        if (dataHome && *dataHome == '/')
            return std::filesystem::path(dataHome);
        return home && *home ? std::filesystem::path(home) / ".local" / "share" : std::filesystem::path();
    }
    return home && *home ? std::filesystem::path(home) / "Documents" : std::filesystem::path();
#endif
}

} // namespace BRITE::Content
