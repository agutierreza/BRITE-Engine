#pragma once

// SHA-256 (FIPS 180-4), self-contained, so the content module needs no cryptography library to say whether a file is
// the one a manifest names. It identifies content; it does not authenticate anyone -- a manifest an application
// trusts must reach it some other way (a signature, a secure channel), which is the application's business.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace BRITE::Content {

using Sha256Digest = std::array<std::uint8_t, 32>;

/// Feed bytes in any number of pieces, then Finish() once.
class Sha256 {
  public:
    Sha256();
    void Update(std::span<const std::uint8_t> bytes);
    void Update(std::string_view text);
    /// The digest of everything fed. The object is spent afterwards: feeding it again is a caller's mistake.
    Sha256Digest Finish();

  private:
    void Block(const std::uint8_t* block);

    std::array<std::uint32_t, 8> m_state{};
    std::array<std::uint8_t, 64> m_buffer{};
    std::size_t m_buffered = 0;
    std::uint64_t m_totalBytes = 0;
};

Sha256Digest Sha256Of(std::span<const std::uint8_t> bytes);
Sha256Digest Sha256Of(std::string_view text);

/// 64 lowercase hex digits.
std::string ToHex(const Sha256Digest& digest);
/// The digest 64 hex digits spell (either case), or nothing for any other text.
std::optional<Sha256Digest> DigestFromHex(std::string_view hex);

} // namespace BRITE::Content
