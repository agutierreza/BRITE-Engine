#pragma once

// What an application shipped: every file, its size and its SHA-256, and which build it was.
//
// A build writes one (brite_content_manifest in CMake, cmake/BriteContent.cmake) beside the executable, after every
// file it names is in place. At run time a ContentRoot reads it to refuse a file the build did not ship, or one that
// changed since; an updater compares two of them (Changed) to learn which files to fetch.
//
// The file is JSON:
//
//     { "app": "...", "version": "...", "sequence": 7,
//       "properties": { "key": "value", ... },
//       "files": [ { "path": "models/a.glb", "size": 1234, "sha256": "<64 hex digits>" }, ... ] }
//
// `sequence` only grows from one release of an application to the next, so a reader can refuse an older manifest
// presented as a newer one. `properties` are the application's own facts (strings), carried and never read here.
// Paths are content names (IsContentName): relative, '/'-separated, and unique ignoring case -- two names that differ
// only in case are one file on Windows.

#include <Content/Sha256.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BRITE::Content {

/// Whether `name` is a content name: '/'-separated segments of A-Z, a-z, 0-9, '.', '_', '-' and '+', none empty, none
/// "." or "..", none ending in '.' (Windows drops a trailing dot, so "a." would open "a"), and no leading '/'. So a
/// name can never reach outside the folder it is looked up in -- no parent, no drive, no share, no backslash.
bool IsContentName(std::string_view name);

/// `name` folded to lower case, for comparing names as Windows does.
std::string FoldedName(std::string_view name);

struct ManifestEntry {
    std::string path;
    std::uint64_t size = 0;
    Sha256Digest sha256{};
};

struct Manifest {
    std::string app;
    std::string version;
    std::uint64_t sequence = 0;
    std::map<std::string, std::string> properties;
    /// In path order (byte order), as Parse leaves them and ToJson writes them.
    std::vector<ManifestEntry> files;

    /// The manifest `json` spells, or nothing with `error` saying why: not JSON, a field missing or of the wrong
    /// type, a path that is no content name, a size that is not a whole number, a hash that is not 64 hex digits, or
    /// two paths that are one file ignoring case.
    static std::optional<Manifest> Parse(std::string_view json, std::string& error);

    /// The manifest as JSON, keys in the order above, files in path order: the same manifest always gives the same
    /// text.
    std::string ToJson() const;

    /// The entry for `path`, compared ignoring case, or null.
    const ManifestEntry* Find(std::string_view path) const;
};

/// The entries of `to` that `from` lacks, or holds with another size or hash: what an updater must fetch to turn a
/// folder that matches `from` into one that matches `to`. Compared by path ignoring case; in `to`'s order.
std::vector<ManifestEntry> Changed(const Manifest& from, const Manifest& to);

} // namespace BRITE::Content
