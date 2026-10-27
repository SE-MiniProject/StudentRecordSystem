#include "auth/login_route.h"

#include <optional>
#include <string>
#include <utility>

#include "api/error_response.h"
#include "auth/user_store.h"
#include "httplib.h"
#include "nlohmann/json.hpp"

namespace srs {
namespace {

// The credentials read from the request body. Returning them as one value keeps
// the parse step from threading three out-parameters through the handler, and
// makes "both are present" a property of the type rather than a convention.
struct Credentials {
    std::string username;
    std::string password;
};

// What ReadCredentials found. `missing_field` is empty on success, otherwise the
// first field that was absent or malformed ("body" for an unparseable payload).
struct CredentialsResult {
    std::optional<Credentials> credentials;
    std::string missing_field;
};

// Reads `username` and `password` from the body. Parsing errors, non-string
// values and empty strings all count as missing, because the client contract
// only ever sends non-empty strings.
CredentialsResult ReadCredentials(const httplib::Request& req) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::exception&) {
        return {std::nullopt, "body"};
    }
    if (!body.is_object()) {
        return {std::nullopt, "body"};
    }

    const auto username_it = body.find("username");
    const auto password_it = body.find("password");

    const auto as_non_empty_string = [](nlohmann::json::const_iterator it,
                                        const nlohmann::json& parent) -> std::optional<std::string> {
        if (it == parent.end() || !it->is_string()) {
            return std::nullopt;
        }
        auto value = it->get<std::string>();
        if (value.empty()) {
            return std::nullopt;
        }
        return value;
    };

    auto username = as_non_empty_string(username_it, body);
    if (!username.has_value()) {
        return {std::nullopt, "username"};
    }
    auto password = as_non_empty_string(password_it, body);
    if (!password.has_value()) {
        return {std::nullopt, "password"};
    }

    return {Credentials{std::move(*username), std::move(*password)}, std::string()};
}

std::string RequestId(const httplib::Request& req) {
    return req.get_header_value("X-Request-Id");
}

// One body for every credential failure. Unknown username, wrong password and a
// locked account are indistinguishable here on purpose: a body that differs by
// case lets an attacker enumerate usernames (SR-008).
void SendUnauthenticated(const httplib::Request& req, httplib::Response& res) {
    SendError(res, 401, "unauthenticated", "invalid username or password", RequestId(req));
}

void SendValidationFailed(const httplib::Request& req, httplib::Response& res,
                          const std::string& field) {
    SendError(res, 400, "validation_failed", "missing or malformed field: " + field, RequestId(req),
              field, "required");
}

}  // namespace

void RegisterLoginRoute(httplib::Server& server, UserStore& store) {
    server.Post("/api/auth/login", [&store](const httplib::Request& req, httplib::Response& res) {
        const auto parsed = ReadCredentials(req);
        if (!parsed.credentials.has_value()) {
            SendValidationFailed(req, res, parsed.missing_field);
            return;
        }
        const auto& credentials = *parsed.credentials;

        // Look up first, then verify. A missing user still returns the generic
        // 401, and does so through the same path as a wrong password so the two
        // cannot drift apart in body.
        const auto user = store.Find(credentials.username);
        if (!user.has_value()) {
            SendUnauthenticated(req, res);
            return;
        }

        if (store.VerifyPassword(*user, credentials.password) != VerifyResult::kOk) {
            SendUnauthenticated(req, res);
            return;
        }

        const auto session = store.IssueSession(user->username);

        res.status = 200;
        res.set_content(
            nlohmann::json{
                {"token", session.token},
                {"expiresAt", session.expires_at_iso8601},
                {"user",
                 {
                     {"username", user->username},
                     {"fullName", user->full_name},
                     {"role", user->role},
                 }},
            }
                .dump(),
            "application/json");
    });
}

}  // namespace srs
