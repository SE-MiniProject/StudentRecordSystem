#include "auth/password_hash.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace srs {
namespace password {
namespace {

constexpr int kSaltBytes = 16;    // 32 hex chars, per the contract.
constexpr int kDigestBytes = 32;  // sha256, 64 hex chars.

constexpr char kHex[] = "0123456789abcdef";

// `byte_count` is a count of bytes, not of hex characters, so the result is
// twice as long. Named for the unit because the caller's constant is in bytes.
std::string ToHex(const unsigned char* bytes, int byte_count) {
    std::string out;
    out.reserve(static_cast<std::size_t>(byte_count) * 2);
    for (int i = 0; i < byte_count; ++i) {
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    return out;
}

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// nullopt for any non-hex character or an odd-length string.
std::optional<std::vector<unsigned char>> FromHex(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<unsigned char> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = HexNibble(hex[i]);
        const int lo = HexNibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return out;
}

}  // namespace

std::optional<std::string> GenerateSalt() {
    unsigned char raw[kSaltBytes];
    // RAND_bytes returning 0 means the CSPRNG failed. There is no safe fallback:
    // a predictable salt defeats the point, so report failure rather than
    // degrade to something guessable.
    if (RAND_bytes(raw, kSaltBytes) != 1) {
        return std::nullopt;
    }
    return ToHex(raw, kSaltBytes);
}

std::optional<std::string> Hash(const std::string& password, const std::string& salt_hex) {
    const auto salt = FromHex(salt_hex);
    if (!salt.has_value()) {
        return std::nullopt;
    }

    // Digest salt || password. Prepending the salt means the digest cannot be
    // precomputed without it, which is the whole point of per-row salts.
    std::vector<unsigned char> input(*salt);
    input.insert(input.end(), password.begin(), password.end());

    unsigned char digest[kDigestBytes];
    unsigned int digest_len = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &digest_len, EVP_sha256(), nullptr) != 1) {
        return std::nullopt;
    }
    return ToHex(digest, static_cast<int>(digest_len));
}

bool ConstantTimeEqual(const std::string& a, const std::string& b) {
    // Length is not secret (both are 64 hex chars), so early-returning on it
    // leaks nothing; the byte comparison itself must not short-circuit.
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

}  // namespace password
}  // namespace srs
