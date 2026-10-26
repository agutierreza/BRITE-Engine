#pragma once

// The one way an application names a file it shipped, and reads it -- the same in development and in a build a user
// is handed.
//
// A ContentRoot is a SOURCE of files (today a plain folder, FolderSource) and, optionally, the MANIFEST the build
// wrote for it. Every shipped file is named by a content name ("models/a.glb", IsContentName), never by a path, so
// nothing an application builds can reach outside the folder, and nothing depends on the working directory.
//
// STRICT, when it has a manifest: a name the manifest lacks is refused, a file whose size differs from the manifest's
// is refused by Resolve, and one whose size or SHA-256 differs is refused by Read. So a file the build did not ship,
// or one damaged or replaced since, is never used quietly. Without a manifest (a developer's tree, a test) every
// well-formed name in the folder is served.
//
// Every access is reported to the tap, if one is set (SetTap): a guard -- a test that every file an application reads
// is in its manifest, or a log of reads refused -- watches here.
//
// THE SEAM. IContentSource is what a ContentRoot reads from. FolderSource reads loose files. A source that reads from
// an archive -- PhysicsFS mounting a pack, say -- implements the same three calls, and every reader above it is
// unchanged; such a source has no plain path to give (PlainPath returns nothing), so a caller that must hand a path to
// a library reads the bytes instead. Loose files are the default because they let an update replace one file at a
// time and let any library open them as they are.

#include <Content/Manifest.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BRITE::Content {

/// Where a ContentRoot's files come from. `name` is always a content name the root has already checked.
class IContentSource {
  public:
    virtual ~IContentSource() = default;
    /// The file's size in bytes, or nothing if there is no such file.
    virtual std::optional<std::uint64_t> Size(std::string_view name) const = 0;
    /// The file's bytes, or nothing if it could not be read.
    virtual std::optional<std::vector<std::uint8_t>> Read(std::string_view name) const = 0;
    /// A plain file a library can open by path, or nothing if this source has none (an archive).
    virtual std::optional<std::filesystem::path> PlainPath(std::string_view name) const = 0;
};

/// Loose files under one folder.
class FolderSource final : public IContentSource {
  public:
    explicit FolderSource(std::filesystem::path folder);
    const std::filesystem::path& Folder() const {
        return m_folder;
    }
    std::optional<std::uint64_t> Size(std::string_view name) const override;
    std::optional<std::vector<std::uint8_t>> Read(std::string_view name) const override;
    std::optional<std::filesystem::path> PlainPath(std::string_view name) const override;

  private:
    std::filesystem::path m_folder;
};

/// One access, as the tap sees it.
struct ContentAccess {
    enum class Kind {
        Resolved, ///< a plain path was given out for the name
        Read,     ///< the name's bytes were read and handed out
        Refused,  ///< the name was refused; `reason` says why
    };
    Kind kind = Kind::Refused;
    std::string name;
    std::string reason;
};

/// What Verify found: every manifest entry that is missing or differs, in words. Empty when the source matches.
struct VerifyReport {
    std::vector<std::string> problems;
    bool Clean() const {
        return problems.empty();
    }
};

enum class VerifyDepth {
    Size, ///< every file present with the manifest's size: a stat each, cheap
    Hash, ///< and every file's SHA-256: reads everything
};

class ContentRoot {
  public:
    using Tap = std::function<void(const ContentAccess&)>;

    /// A root over `source`, strict if `manifest` is given.
    explicit ContentRoot(std::shared_ptr<const IContentSource> source, std::optional<Manifest> manifest = std::nullopt);

    /// The folder `folder` as a root, with the manifest file `manifestName` inside it if there is one. A manifest file
    /// that is there but cannot be parsed is an error, never silently ignored: the root is then strict over an EMPTY
    /// manifest, so every name is refused, and `error` says why.
    static ContentRoot FromFolder(const std::filesystem::path& folder, std::string_view manifestName,
                                  std::string* error = nullptr);

    bool IsStrict() const {
        return m_manifest.has_value();
    }
    const Manifest* GetManifest() const {
        return m_manifest ? &*m_manifest : nullptr;
    }
    const IContentSource& Source() const {
        return *m_source;
    }

    /// A plain path to `name`'s file, or nothing -- refused (a bad name; strict: not in the manifest, or of another
    /// size), missing, or a source with no plain files. For a library that opens files itself.
    std::optional<std::filesystem::path> Resolve(std::string_view name) const;

    /// `name`'s bytes, or nothing -- refused as Resolve refuses, unreadable, or (strict) of another size or SHA-256
    /// than the manifest's. `error`, if given, says why.
    std::optional<std::vector<std::uint8_t>> Read(std::string_view name, std::string* error = nullptr) const;

    /// The content name of `path` when it is a plain file inside a FolderSource's folder, else nothing: for a caller
    /// handed a path (a library's file callback) that must ask the root about it.
    std::optional<std::string> NameOf(const std::filesystem::path& path) const;

    /// Checks the source against the manifest (nothing to check without one).
    VerifyReport Verify(VerifyDepth depth) const;

    void SetTap(Tap tap) {
        m_tap = std::move(tap);
    }

  private:
    /// Why `name` is refused before its bytes are read, or empty.
    std::string Refusal(std::string_view name) const;
    void Report(ContentAccess::Kind kind, std::string_view name, std::string reason = {}) const;

    std::shared_ptr<const IContentSource> m_source;
    std::optional<Manifest> m_manifest;
    Tap m_tap;
};

} // namespace BRITE::Content
