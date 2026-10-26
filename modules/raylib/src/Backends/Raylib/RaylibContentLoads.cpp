#include <Backends/Raylib/RaylibContentLoads.hpp>

#include <raylib.h>

#include <climits>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace BRITE::Raylib {

namespace {

FileLoadRouting g_routing;

/// raylib's names are UTF-8.
std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

/// The content name raylib means by `asked`, or nothing if it lies outside the root. raylib builds some names itself:
/// a model's external texture or material is looked up as "<the model's directory>/<file>", and the directory of a
/// model named with no folder is ".", so "./a.png" -- normalised, that is the content name "a.png".
std::optional<std::string> NameOf(const Content::ContentRoot& root, std::string_view asked) {
    if (Content::IsContentName(asked))
        return std::string(asked);
    const std::filesystem::path path = PathOf(asked);
    if (path.is_relative()) {
        const std::u8string normal = path.lexically_normal().generic_u8string();
        const std::string name(reinterpret_cast<const char*>(normal.data()), normal.size());
        if (Content::IsContentName(name))
            return name;
    }
    return root.NameOf(path);
}

/// The bytes raylib asked for, or nothing: through the root if the name is inside it, else plain or refused.
std::optional<std::vector<std::uint8_t>> RoutedRead(const char* fileName) {
    const Content::ContentRoot& root = *g_routing.root;
    const std::string_view asked(fileName);
    if (const std::optional<std::string> name = NameOf(root, asked)) {
        std::string error;
        std::optional<std::vector<std::uint8_t>> bytes = root.Read(*name, &error);
        if (!bytes)
            TraceLog(LOG_WARNING, "CONTENT: [%s] refused: %s", fileName, error.c_str());
        return bytes;
    }
    if (g_routing.outside)
        g_routing.outside(asked);
    if (g_routing.refuseOutsideRoot) {
        TraceLog(LOG_WARNING, "CONTENT: [%s] is outside the content root: refused", fileName);
        return std::nullopt;
    }
    std::ifstream in(PathOf(asked), std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Both are called from C, through raylib: nothing may leave them by an exception, so any failure is a failed load.
// Both buffers are released by raylib (UnloadFileData, UnloadFileText) with RL_FREE, so they are allocated with
// raylib's own MemAlloc.
unsigned char* LoadData(const char* fileName, int* dataSize) {
    *dataSize = 0;
    try {
        const std::optional<std::vector<std::uint8_t>> bytes = RoutedRead(fileName);
        if (!bytes || bytes->empty() || bytes->size() > static_cast<std::size_t>(INT_MAX))
            return nullptr;
        auto* data = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(bytes->size())));
        if (!data)
            return nullptr;
        std::memcpy(data, bytes->data(), bytes->size());
        *dataSize = static_cast<int>(bytes->size());
        return data;
    } catch (...) {
        TraceLog(LOG_WARNING, "CONTENT: [%s] could not be read", fileName);
        return nullptr;
    }
}

char* LoadText(const char* fileName) {
    try {
        const std::optional<std::vector<std::uint8_t>> bytes = RoutedRead(fileName);
        if (!bytes || bytes->size() >= static_cast<std::size_t>(INT_MAX))
            return nullptr;
        auto* text = static_cast<char*>(MemAlloc(static_cast<unsigned int>(bytes->size() + 1)));
        if (!text)
            return nullptr;
        if (!bytes->empty())
            std::memcpy(text, bytes->data(), bytes->size());
        text[bytes->size()] = '\0';
        return text;
    } catch (...) {
        TraceLog(LOG_WARNING, "CONTENT: [%s] could not be read", fileName);
        return nullptr;
    }
}

} // namespace

void RouteFileLoads(FileLoadRouting routing) {
    g_routing = std::move(routing);
    SetLoadFileDataCallback(g_routing.root ? LoadData : nullptr);
    SetLoadFileTextCallback(g_routing.root ? LoadText : nullptr);
}

const FileLoadRouting& CurrentFileLoadRouting() {
    return g_routing;
}

ScopedFileLoadRouting::ScopedFileLoadRouting(FileLoadRouting routing) : m_previous(g_routing) {
    RouteFileLoads(std::move(routing));
}

ScopedFileLoadRouting::~ScopedFileLoadRouting() {
    RouteFileLoads(std::move(m_previous));
}

} // namespace BRITE::Raylib
