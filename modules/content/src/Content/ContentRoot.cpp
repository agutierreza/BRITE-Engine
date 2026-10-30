#include <Content/ContentRoot.hpp>

#include <fstream>
#include <iterator>
#include <system_error>

namespace BRITE::Content {

namespace {

std::filesystem::path Absolute(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

/// `path`, '/'-separated, as UTF-8 bytes.
std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::optional<std::vector<std::uint8_t>> ReadWholeFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
        return std::nullopt;
    return bytes;
}

} // namespace

FolderSource::FolderSource(std::filesystem::path folder) : m_folder(Absolute(folder)) {}

std::optional<std::uint64_t> FolderSource::Size(std::string_view name) const {
    std::error_code ec;
    const std::filesystem::path file = m_folder / std::filesystem::path(std::string(name));
    if (!std::filesystem::is_regular_file(file, ec) || ec)
        return std::nullopt;
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec)
        return std::nullopt;
    return static_cast<std::uint64_t>(size);
}

std::optional<std::vector<std::uint8_t>> FolderSource::Read(std::string_view name) const {
    return ReadWholeFile(m_folder / std::filesystem::path(std::string(name)));
}

std::optional<std::filesystem::path> FolderSource::PlainPath(std::string_view name) const {
    return m_folder / std::filesystem::path(std::string(name));
}

ContentRoot::ContentRoot(std::shared_ptr<const IContentSource> source, std::optional<Manifest> manifest)
    : m_source(std::move(source)), m_manifest(std::move(manifest)) {}

ContentRoot ContentRoot::FromFolder(const std::filesystem::path& folder, std::string_view manifestName,
                                    std::string* error) {
    auto source = std::make_shared<FolderSource>(folder);
    const std::filesystem::path manifestFile = source->Folder() / std::filesystem::path(std::string(manifestName));
    if (manifestName.empty())
        return ContentRoot(source);
    std::error_code ec;
    const bool there = std::filesystem::exists(manifestFile, ec);
    if (ec) {
        // Neither there nor not there: the manifest may be, so the root is not left loose.
        if (error)
            *error = Utf8(manifestFile) + ": could not be checked: " + ec.message();
        return ContentRoot(source, Manifest{});
    }
    if (!there)
        return ContentRoot(source);
    std::string why;
    const std::optional<std::vector<std::uint8_t>> bytes = ReadWholeFile(manifestFile);
    std::optional<Manifest> manifest;
    if (!bytes) {
        why = "could not be read";
    } else {
        manifest = Manifest::Parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), why);
    }
    if (!manifest) {
        if (error)
            *error = Utf8(manifestFile) + ": " + why;
        return ContentRoot(source, Manifest{});
    }
    return ContentRoot(source, std::move(manifest));
}

std::string ContentRoot::Refusal(std::string_view name) const {
    if (!IsContentName(name))
        return "not a content name";
    const std::optional<std::uint64_t> size = m_source->Size(name);
    if (!m_manifest)
        return size ? std::string() : "no such file";
    const ManifestEntry* entry = m_manifest->Find(name);
    if (!entry)
        return "not in the manifest";
    if (!size)
        return "in the manifest but missing";
    if (*size != entry->size) {
        return "size " + std::to_string(*size) + " differs from the manifest's " + std::to_string(entry->size);
    }
    return {};
}

void ContentRoot::Report(ContentAccess::Kind kind, std::string_view name, std::string reason) const {
    if (m_tap)
        m_tap(ContentAccess{kind, std::string(name), std::move(reason)});
}

std::optional<std::filesystem::path> ContentRoot::Resolve(std::string_view name) const {
    std::string refusal = Refusal(name);
    std::optional<std::filesystem::path> path;
    if (refusal.empty()) {
        path = m_source->PlainPath(name);
        if (!path)
            refusal = "this source has no plain files";
    }
    if (!refusal.empty()) {
        Report(ContentAccess::Kind::Refused, name, std::move(refusal));
        return std::nullopt;
    }
    Report(ContentAccess::Kind::Resolved, name);
    return path;
}

std::optional<std::vector<std::uint8_t>> ContentRoot::Read(std::string_view name, std::string* error) const {
    std::string refusal = Refusal(name);
    std::optional<std::vector<std::uint8_t>> bytes;
    if (refusal.empty()) {
        bytes = m_source->Read(name);
        if (!bytes) {
            refusal = "could not be read";
        } else if (m_manifest) {
            // The size again (the file may have changed since Refusal looked) and the hash: what is handed out is
            // exactly what the build shipped.
            const ManifestEntry* entry = m_manifest->Find(name);
            if (bytes->size() != entry->size) {
                refusal = "size " + std::to_string(bytes->size()) + " differs from the manifest's " +
                          std::to_string(entry->size);
            } else if (Sha256Of(*bytes) != entry->sha256) {
                refusal = "its SHA-256 differs from the manifest's";
            }
        }
    }
    if (!refusal.empty()) {
        if (error)
            *error = std::string(name) + ": " + refusal;
        Report(ContentAccess::Kind::Refused, name, std::move(refusal));
        return std::nullopt;
    }
    Report(ContentAccess::Kind::Read, name);
    return bytes;
}

std::optional<std::string> ContentRoot::NameOf(const std::filesystem::path& path) const {
    const auto* folder = dynamic_cast<const FolderSource*>(m_source.get());
    if (!folder)
        return std::nullopt;
    // Compared as Windows compares paths, ignoring case; the name keeps the caller's spelling. As UTF-8, never in the
    // ANSI code page, which cannot spell every folder and throws when it cannot.
    const std::string inside = Utf8(Absolute(path));
    std::string prefix = Utf8(folder->Folder());
    if (prefix.empty() || prefix.back() != '/')
        prefix += '/';
    if (inside.size() <= prefix.size() || FoldedName(inside.substr(0, prefix.size())) != FoldedName(prefix)) {
        return std::nullopt;
    }
    std::string name = inside.substr(prefix.size());
    if (!IsContentName(name))
        return std::nullopt;
    return name;
}

VerifyReport ContentRoot::Verify(VerifyDepth depth) const {
    VerifyReport report;
    if (!m_manifest)
        return report;
    for (const ManifestEntry& entry : m_manifest->files) {
        const std::optional<std::uint64_t> size = m_source->Size(entry.path);
        if (!size) {
            report.problems.push_back(entry.path + ": missing");
            continue;
        }
        if (*size != entry.size) {
            report.problems.push_back(entry.path + ": size " + std::to_string(*size) + ", the manifest says " +
                                      std::to_string(entry.size));
            continue;
        }
        if (depth == VerifyDepth::Hash) {
            const std::optional<std::vector<std::uint8_t>> bytes = m_source->Read(entry.path);
            if (!bytes) {
                report.problems.push_back(entry.path + ": could not be read");
            } else if (Sha256Of(*bytes) != entry.sha256) {
                report.problems.push_back(entry.path + ": its SHA-256 differs from the manifest's");
            }
        }
    }
    return report;
}

} // namespace BRITE::Content
