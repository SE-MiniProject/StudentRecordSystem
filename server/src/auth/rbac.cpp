#include "auth/rbac.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "api/error_response.h"
#include "auth/bearer_token.h"
#include "auth/session_store.h"
#include "httplib.h"

namespace srs {

std::optional<Role> ParseRole(std::string_view text) {
    if (text == "admin") {
        return Role::kAdmin;
    }
    if (text == "teacher") {
        return Role::kTeacher;
    }
    if (text == "student") {
        return Role::kStudent;
    }
    return std::nullopt;
}

std::string_view RoleName(Role role) {
    switch (role) {
        case Role::kAdmin:
            return "admin";
        case Role::kTeacher:
            return "teacher";
        case Role::kStudent:
            return "student";
    }
    // Unreachable while every enumerator is handled above; the switch is
    // exhaustive rather than defaulted so adding a role is a compile error.
    return "unknown";
}

RbacGuard::RbacGuard(SessionStore& sessions) : sessions_(sessions) {}

GuardResult RbacGuard::Authenticate(const httplib::Request& req, Principal* out) const {
    const auto token = BearerToken(req);
    if (token.empty()) {
        return GuardResult::kUnauthenticated;
    }

    const auto identity = sessions_.Resolve(token);
    if (!identity.has_value()) {
        return GuardResult::kUnauthenticated;
    }

    // A session whose stored role is not one of the three is treated as
    // forbidden rather than unauthenticated: the caller proved who they are, so
    // the failure is authorisation. Refusing to guess is the point — a fourth
    // role cannot be invented by writing a new string into the users table.
    const auto role = ParseRole(identity->role);
    if (!role.has_value()) {
        return GuardResult::kForbidden;
    }

    out->username = identity->username;
    out->role = *role;
    return GuardResult::kAllow;
}

GuardResult RbacGuard::RequireRole(const httplib::Request& req, const std::vector<Role>& allowed,
                                   Principal* out) const {
    Principal principal;
    const auto authenticated = Authenticate(req, &principal);
    if (authenticated != GuardResult::kAllow) {
        return authenticated;
    }

    if (allowed.empty()) {
        *out = std::move(principal);
        return GuardResult::kAllow;
    }

    if (std::find(allowed.begin(), allowed.end(), principal.role) == allowed.end()) {
        return GuardResult::kForbidden;
    }

    *out = std::move(principal);
    return GuardResult::kAllow;
}

GuardResult RbacGuard::RequireRoleOrOwnership(const httplib::Request& req,
                                              const std::vector<Role>& allowed,
                                              const std::string& owned_by_username,
                                              Principal* out) const {
    Principal principal;
    const auto authenticated = Authenticate(req, &principal);
    if (authenticated != GuardResult::kAllow) {
        return authenticated;
    }

    const bool role_permits =
        std::find(allowed.begin(), allowed.end(), principal.role) != allowed.end();
    // Ownership is only a fallback, never a substitute for authentication: the
    // username compared here came from the session store, not from the request.
    const bool owns = principal.username == owned_by_username;

    if (!role_permits && !owns) {
        return GuardResult::kForbidden;
    }

    *out = std::move(principal);
    return GuardResult::kAllow;
}

bool GuardAllows(GuardResult result, const httplib::Request& req, httplib::Response& res) {
    const auto request_id = req.get_header_value("X-Request-Id");
    switch (result) {
        case GuardResult::kAllow:
            return true;
        case GuardResult::kUnauthenticated:
            SendError(res, 401, "unauthenticated", "invalid or missing bearer token", request_id);
            return false;
        case GuardResult::kForbidden:
            SendError(res, 403, "forbidden", "role or ownership not permitted", request_id);
            return false;
    }
    return false;
}

}  // namespace srs