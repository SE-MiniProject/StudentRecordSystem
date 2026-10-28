#pragma once

#include <string>

namespace httplib {
class Request;
}  // namespace httplib

namespace srs {

// The bearer token on `req`, or an empty string when the header is absent or
// not a Bearer credential.
//
// This lives apart from the RBAC guard because it is not an authorization
// concept: logout parses a token to revoke it, and has no interest in roles.
// Keeping it here means the header format is parsed in exactly one place
// without every consumer dragging in the role model.
std::string BearerToken(const httplib::Request& req);

}  // namespace srs