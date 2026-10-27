#pragma once

#include <optional>
#include <string>

namespace srs {

// Salted password hashing, per SR-003 and the hash column rules in the API
// contract: a 16-byte random salt and a 64-hex-char digest.
//
// Nothing here throws. These run on the login request path, where an exception
// has no sensible recovery: there is no partial result to return and no way for
// the route to distinguish "the CSPRNG is broken" from a bug. So failures are
// reported as nullopt and the caller decides. The route maps them to the generic
// 401, which is also what a wrong password produces, so a broken CSPRNG does not
// become an oracle either (SR-008).
namespace password {

// A random 16-byte salt as 32 lowercase hex chars, or nullopt if the CSPRNG
// failed. Uses RAND_bytes, not rand().
std::optional<std::string> GenerateSalt();

// The digest of `password` with `salt_hex`, as 64 lowercase hex chars, or
// nullopt if `salt_hex` is not valid hex. The salt is decoded from hex and
// prepended to the password bytes before hashing, so two accounts with the same
// password do not share a digest.
std::optional<std::string> Hash(const std::string& password, const std::string& salt_hex);

// Constant-time comparison of two hex digests. A short-circuiting == leaks how
// many leading characters matched, which is enough to walk a digest out.
bool ConstantTimeEqual(const std::string& a, const std::string& b);

}  // namespace password

}  // namespace srs
