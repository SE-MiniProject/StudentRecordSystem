#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace httplib {
class Request;
class Response;
}  // namespace httplib

namespace srs {

class SessionStore;

// The three roles the contract recognises. Parsed from the stored string once,
// at the boundary, so the rest of the server compares enums rather than
// strings: a typo in a role column becomes "unknown role, deny" instead of
// quietly matching nothing and being treated as a fourth role.
enum class Role {
    kAdmin,
    kTeacher,
    kStudent,
};

// The role a stored string denotes, or nullopt when it is not one of the three.
// Unknown is deliberately not a Role value: a value would have to have an
// opinion about what it may access.
std::optional<Role> ParseRole(std::string_view text);

// The wire spelling, for tests and for echoing a role back in a response.
std::string_view RoleName(Role role);

// The caller behind a verified token: who they are, and the role the *server*
// resolved. The client's own role claim is never consulted (SR-004).
struct Principal {
    std::string username;
    Role role;
};

// What a guard call decided:
//   kAllow  — the request may proceed.
//   kUnauthenticated — no token, or a token that resolves to nothing: 401.
//   kForbidden — an authenticated caller whose role or ownership fails: 403.
enum class GuardResult {
    kAllow,
    kUnauthenticated,
    kForbidden,
};

// The RBAC gate every authenticated route runs before its handler body.
//
// A class rather than free functions because the guard must hold the session
// store, and passing that store through every route by hand is how one route
// ends up forgetting to check. Construct one per server and hand it to the
// routes; the store is the only dependency.
class RbacGuard {
public:
    explicit RbacGuard(SessionStore& sessions);

    // Resolves the bearer token on `req`. No policy beyond "is this a live
    // session": used by routes whose only requirement is being authenticated.
    //
    // `out` receives the principal on success and is untouched otherwise, so a
    // caller that ignores the result cannot read a half-filled identity.
    GuardResult Authenticate(const httplib::Request& req, Principal* out) const;

    // Authenticate, then require the caller's role to be one of `allowed`.
    // An empty list means "authenticated only", which is why routes with no role
    // requirement do not have to invent a sentinel role.
    GuardResult RequireRole(const httplib::Request& req, const std::vector<Role>& allowed,
                            Principal* out) const;

    // Authenticate, then require either an allowed role or ownership of the
    // addressed record. `owns` is asked only when the role check fails, so a
    // teacher does not pay for an ownership query.
    //
    // This is the shape every "(Student (self))" route needs; expressing it once
    // here is what keeps "students may read their own grades" from being
    // reimplemented, slightly differently, in three handlers.
    GuardResult RequireRoleOrOwnership(const httplib::Request& req,
                                       const std::vector<Role>& allowed,
                                       const std::string& owned_by_username,
                                       Principal* out) const;

private:
    SessionStore& sessions_;
};

// Writes the response a failed guard implies: 401 unauthenticated or 403
// forbidden, in the shared error envelope. Returns false when the result was
// kAllow, so a route reads as:
//
//   if (!GuardAllows(guard.RequireRole(req, {Role::kAdmin}, &who), req, res)) {
//       return;
//   }
//
// One helper because three guarded routes must not disagree about what a 403
// body looks like.
bool GuardAllows(GuardResult result, const httplib::Request& req, httplib::Response& res);

}  // namespace srs