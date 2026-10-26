#pragma once

// A folder of a test's own under the system's temporary folder, removed with this object. Named by the process, a
// clock and a counter, so two cases -- or two runs at once -- never share one.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#define BRITE_TEST_PID _getpid()
#else
#include <unistd.h>
#define BRITE_TEST_PID getpid()
#endif

namespace BRITE::Test {

struct ScratchFolder {
    std::filesystem::path path;
    explicit ScratchFolder(std::string_view purpose) {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("brite_" + std::string(purpose) + "_" + std::to_string(BRITE_TEST_PID) + "_" + std::to_string(stamp) +
                "_" + std::to_string(counter++));
        std::filesystem::create_directories(path);
    }
    ~ScratchFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    ScratchFolder(const ScratchFolder&) = delete;
    ScratchFolder& operator=(const ScratchFolder&) = delete;

    /// Writes `bytes` to `relative` inside the folder, making its parent folders.
    std::filesystem::path Write(std::string_view relative, std::string_view bytes) const {
        const std::filesystem::path file = path / std::filesystem::path(std::string(relative));
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return file;
    }
};

inline std::string ReadText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace BRITE::Test
