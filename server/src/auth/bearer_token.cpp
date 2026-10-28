#include "auth/bearer_token.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

#include "httplib.h"

namespace srs {
namespace {

constexpr const char* kBearerPrefix = "Bearer ";

// RFC 7235 defines the auth-scheme as case-insensitive, so "bearer" and
// "BEARER" are both valid. The token itself is not case-folded.
bool EqualsIgnoringCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string BearerToken(const httplib::Request& req) {
    const auto header = req.get_header_value("Authorization");
    const std::string_view prefix{kBearerPrefix};
    if (header.size() <= prefix.size()) {
        return std::string();
    }
    if (!EqualsIgnoringCase(std::string_view{header}.substr(0, prefix.size()), prefix)) {
        return std::string();
    }
    return header.substr(prefix.size());
}

}  // namespace srs