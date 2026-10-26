#include <Content/Manifest.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

namespace BRITE::Content {

namespace {

bool IsNameCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-' || c == '+';
}

} // namespace

bool IsContentName(std::string_view name) {
    if (name.empty() || name.front() == '/')
        return false;
    for (const char c : name) {
        if (!IsNameCharacter(c) && c != '/')
            return false;
    }
    // Segment by segment: none empty ("a//b", a trailing '/'), none "." or "..", none ending in '.'.
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t end = name.find('/', start);
        if (end == std::string_view::npos)
            end = name.size();
        const std::string_view segment = name.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == ".." || segment.back() == '.')
            return false;
        start = end + 1;
    }
    return true;
}

std::string FoldedName(std::string_view name) {
    std::string folded(name);
    for (char& c : folded) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return folded;
}

std::optional<Manifest> Manifest::Parse(std::string_view json, std::string& error) {
    const nlohmann::json document = nlohmann::json::parse(json, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        error = "not a JSON object";
        return std::nullopt;
    }
    const auto field = [&](const char* key, bool (nlohmann::json::*is)() const noexcept) -> const nlohmann::json* {
        const auto found = document.find(key);
        if (found == document.end() || !((*found).*is)()) {
            error = std::string("\"") + key + "\" is missing or of the wrong type";
            return nullptr;
        }
        return &*found;
    };
    const nlohmann::json* app = field("app", &nlohmann::json::is_string);
    if (!app)
        return std::nullopt;
    const nlohmann::json* version = field("version", &nlohmann::json::is_string);
    if (!version)
        return std::nullopt;
    const nlohmann::json* sequence = field("sequence", &nlohmann::json::is_number_unsigned);
    if (!sequence)
        return std::nullopt;
    const nlohmann::json* properties = field("properties", &nlohmann::json::is_object);
    if (!properties)
        return std::nullopt;
    const nlohmann::json* files = field("files", &nlohmann::json::is_array);
    if (!files)
        return std::nullopt;

    Manifest manifest;
    manifest.app = app->get<std::string>();
    manifest.version = version->get<std::string>();
    manifest.sequence = sequence->get<std::uint64_t>();
    for (const auto& [key, value] : properties->items()) {
        if (!value.is_string()) {
            error = "property \"" + key + "\" is not a string";
            return std::nullopt;
        }
        manifest.properties[key] = value.get<std::string>();
    }
    std::set<std::string> seen;
    for (const nlohmann::json& file : *files) {
        if (!file.is_object()) {
            error = "a file entry is not an object";
            return std::nullopt;
        }
        const auto path = file.find("path");
        const auto size = file.find("size");
        const auto sha256 = file.find("sha256");
        if (path == file.end() || !path->is_string() || !IsContentName(path->get<std::string>())) {
            error = "a file entry's path is missing or is not a content name";
            return std::nullopt;
        }
        ManifestEntry entry;
        entry.path = path->get<std::string>();
        if (size == file.end() || !size->is_number_unsigned()) {
            error = "\"" + entry.path + "\": size is missing or is not a whole number";
            return std::nullopt;
        }
        entry.size = size->get<std::uint64_t>();
        const std::optional<Sha256Digest> digest =
            sha256 != file.end() && sha256->is_string() ? DigestFromHex(sha256->get<std::string>()) : std::nullopt;
        if (!digest) {
            error = "\"" + entry.path + "\": sha256 is missing or is not 64 hex digits";
            return std::nullopt;
        }
        entry.sha256 = *digest;
        if (!seen.insert(FoldedName(entry.path)).second) {
            error = "\"" + entry.path + "\" is listed twice (ignoring case)";
            return std::nullopt;
        }
        manifest.files.push_back(std::move(entry));
    }
    std::sort(manifest.files.begin(), manifest.files.end(),
              [](const ManifestEntry& a, const ManifestEntry& b) { return a.path < b.path; });
    return manifest;
}

std::string Manifest::ToJson() const {
    nlohmann::ordered_json document;
    document["app"] = app;
    document["version"] = version;
    document["sequence"] = sequence;
    document["properties"] = nlohmann::ordered_json::object();
    for (const auto& [key, value] : properties)
        document["properties"][key] = value;
    std::vector<const ManifestEntry*> ordered;
    for (const ManifestEntry& entry : files)
        ordered.push_back(&entry);
    std::sort(ordered.begin(), ordered.end(),
              [](const ManifestEntry* a, const ManifestEntry* b) { return a->path < b->path; });
    document["files"] = nlohmann::ordered_json::array();
    for (const ManifestEntry* entry : ordered) {
        nlohmann::ordered_json file;
        file["path"] = entry->path;
        file["size"] = entry->size;
        file["sha256"] = ToHex(entry->sha256);
        document["files"].push_back(std::move(file));
    }
    return document.dump(2) + "\n";
}

const ManifestEntry* Manifest::Find(std::string_view path) const {
    const std::string folded = FoldedName(path);
    for (const ManifestEntry& entry : files) {
        if (FoldedName(entry.path) == folded)
            return &entry;
    }
    return nullptr;
}

std::vector<ManifestEntry> Changed(const Manifest& from, const Manifest& to) {
    std::vector<ManifestEntry> changed;
    for (const ManifestEntry& entry : to.files) {
        const ManifestEntry* had = from.Find(entry.path);
        if (!had || had->size != entry.size || had->sha256 != entry.sha256)
            changed.push_back(entry);
    }
    return changed;
}

} // namespace BRITE::Content
