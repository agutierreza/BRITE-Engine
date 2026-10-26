#include <Content/Sha256.hpp>

namespace BRITE::Content {

namespace {

// FIPS 180-4 section 4.2.2: the first 32 bits of the fractional parts of the cube roots of the first 64 primes.
constexpr std::array<std::uint32_t, 64> ROUND_CONSTANTS = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

// Section 5.3.3: the first 32 bits of the fractional parts of the square roots of the first eight primes.
constexpr std::array<std::uint32_t, 8> INITIAL_STATE = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

constexpr std::uint32_t RotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

int HexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

Sha256::Sha256() : m_state(INITIAL_STATE) {}

void Sha256::Block(const std::uint8_t* block) {
    // Section 6.2.2: the message schedule, then 64 rounds over a copy of the state.
    std::array<std::uint32_t, 64> schedule{};
    for (int i = 0; i < 16; ++i) {
        schedule[i] = (std::uint32_t{block[4 * i]} << 24) | (std::uint32_t{block[4 * i + 1]} << 16) |
                      (std::uint32_t{block[4 * i + 2]} << 8) | std::uint32_t{block[4 * i + 3]};
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            RotateRight(schedule[i - 15], 7) ^ RotateRight(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
        const std::uint32_t s1 =
            RotateRight(schedule[i - 2], 17) ^ RotateRight(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }
    std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    std::uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t sum1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + sum1 + choose + ROUND_CONSTANTS[i] + schedule[i];
        const std::uint32_t sum0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

void Sha256::Update(std::span<const std::uint8_t> bytes) {
    m_totalBytes += bytes.size();
    for (const std::uint8_t byte : bytes) {
        m_buffer[m_buffered++] = byte;
        if (m_buffered == m_buffer.size()) {
            Block(m_buffer.data());
            m_buffered = 0;
        }
    }
}

void Sha256::Update(std::string_view text) {
    Update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

Sha256Digest Sha256::Finish() {
    // Section 5.1.1: a 1 bit, zeros to 56 bytes into the last block, then the message's length in bits, big-endian.
    const std::uint64_t lengthInBits = m_totalBytes * 8;
    m_buffer[m_buffered++] = 0x80;
    if (m_buffered > 56) {
        while (m_buffered < 64)
            m_buffer[m_buffered++] = 0;
        Block(m_buffer.data());
        m_buffered = 0;
    }
    while (m_buffered < 56)
        m_buffer[m_buffered++] = 0;
    for (int i = 7; i >= 0; --i)
        m_buffer[m_buffered++] = static_cast<std::uint8_t>(lengthInBits >> (8 * i));
    Block(m_buffer.data());
    m_buffered = 0;

    Sha256Digest digest{};
    for (std::size_t i = 0; i < m_state.size(); ++i) {
        digest[4 * i] = static_cast<std::uint8_t>(m_state[i] >> 24);
        digest[4 * i + 1] = static_cast<std::uint8_t>(m_state[i] >> 16);
        digest[4 * i + 2] = static_cast<std::uint8_t>(m_state[i] >> 8);
        digest[4 * i + 3] = static_cast<std::uint8_t>(m_state[i]);
    }
    return digest;
}

Sha256Digest Sha256Of(std::span<const std::uint8_t> bytes) {
    Sha256 hash;
    hash.Update(bytes);
    return hash.Finish();
}

Sha256Digest Sha256Of(std::string_view text) {
    Sha256 hash;
    hash.Update(text);
    return hash.Finish();
}

std::string ToHex(const Sha256Digest& digest) {
    constexpr const char* DIGITS = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        hex += DIGITS[byte >> 4];
        hex += DIGITS[byte & 0x0f];
    }
    return hex;
}

std::optional<Sha256Digest> DigestFromHex(std::string_view hex) {
    Sha256Digest digest{};
    if (hex.size() != digest.size() * 2)
        return std::nullopt;
    for (std::size_t i = 0; i < digest.size(); ++i) {
        const int high = HexValue(hex[2 * i]);
        const int low = HexValue(hex[2 * i + 1]);
        if (high < 0 || low < 0)
            return std::nullopt;
        digest[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return digest;
}

} // namespace BRITE::Content
