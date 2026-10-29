#include "accounts/account_routes.h"

#include <optional>
#include <string>

#include "accounts/account_store.h"
#include "api/error_response.h"
#include "auth/rbac.h"
#include "auth/session_store.h"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "validation/validation.h"

namespace srs {
namespace {

using json = nlohmann::json;

json AccountToJson(const Account& account) {
    return json{
        {"username", account.username},
        {"fullName", account.full_name},
        {"role", account.role},
        {"active", account.active},
    };
}

std::string StringField(const json& body, const char* key) {
    if (!body.contains(key) || !body[key].is_string()) return std::string();
    return body[key].get<std::string>();
}

void SendFieldErrors(httplib::Response& res, const validation::FieldErrors& errors,
                     const httplib::Request& req) {
    json fields = json::object();
    for (const auto& error : errors.errors) fields[error.field] = error.problem;
    res.status = 400;
    res.set_content(json{{"error",
                          {{"code", "validation_failed"},
                           {"message", "one or more fields are invalid"},
                           {"fields", fields},
                           {"requestId", req.get_header_value("X-Request-Id")}}}}
                        .dump(),
                    "application/json");
}

}  // namespace

void RegisterAccountRoutes(httplib::Server& server, AccountStore& accounts,
                           SessionStore& sessions) {
    const RbacGuard guard(sessions);

    // GET /api/accounts — Admin only (SRS-17 needs this list to edit from).
    server.Get("/api/accounts", [&accounts, guard](const httplib::Request& req,
                                                   httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kAdmin}, &who), req, res)) return;

        const auto list = accounts.List();
        if (!list.has_value()) {
            SendError(res, 500, "internal_error", "could not read accounts",
                      req.get_header_value("X-Request-Id"));
            return;
        }
        json items = json::array();
        for (const auto& account : *list) items.push_back(AccountToJson(account));
        res.status = 200;
        res.set_content(json{{"items", items}, {"total", items.size()}}.dump(),
                        "application/json");
    });

    // POST /api/accounts — Admin only (SRS-19).
    server.Post("/api/accounts", [&accounts, guard](const httplib::Request& req,
                                                    httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kAdmin}, &who), req, res)) return;

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            SendError(res, 400, "invalid_json", "request body must be valid JSON",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        const std::string username = StringField(body, "username");
        const std::string password = StringField(body, "password");
        const std::string full_name = StringField(body, "fullName");
        const std::string role = StringField(body, "role");

        const auto errors =
            validation::ValidateAccountCreate(username, password, full_name, role);
        if (!errors.ok()) {
            SendFieldErrors(res, errors, req);
            return;
        }

        switch (accounts.Create(username, password, full_name, role)) {
            case CreateAccountResult::kOk:
                res.status = 201;
                res.set_content(
                    AccountToJson(Account{username, full_name, role, true}).dump(),
                    "application/json");
                return;
            case CreateAccountResult::kDuplicateUsername:
                SendError(res, 409, "duplicate_username", "that username is already taken",
                          req.get_header_value("X-Request-Id"));
                return;
            case CreateAccountResult::kError:
                SendError(res, 500, "internal_error", "could not create account",
                          req.get_header_value("X-Request-Id"));
                return;
        }
    });

    // PATCH /api/accounts/{username} — Admin only (SRS-17).
    server.Patch(R"(/api/accounts/([^/]+))", [&accounts, guard](const httplib::Request& req,
                                                               httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kAdmin}, &who), req, res)) return;

        const std::string username = req.matches[1].str();

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            SendError(res, 400, "invalid_json", "request body must be valid JSON",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        AccountChanges changes;
        changes.full_name = StringField(body, "fullName");
        changes.role = StringField(body, "role");
        changes.password = StringField(body, "password");
        if (body.contains("active") && body["active"].is_boolean()) {
            changes.active = body["active"].get<bool>();
        }

        // Validation takes its own struct because it must be testable without an
        // AccountStore; the two are converted here rather than sharing a type
        // that would make the store's shape depend on a validation detail.
        validation::AccountPatchFields patch_fields;
        patch_fields.full_name = changes.full_name;
        patch_fields.role = changes.role;
        patch_fields.password = changes.password;
        patch_fields.has_active = changes.active.has_value();
        patch_fields.active = changes.active.value_or(true);

        const auto errors = validation::ValidateAccountPatch(patch_fields);
        if (!errors.ok()) {
            SendFieldErrors(res, errors, req);
            return;
        }

        switch (accounts.Patch(username, changes)) {
            case PatchAccountResult::kOk: {
                // Re-read so the response is the stored account rather than an
                // echo of the request, which would report "unchanged" fields
                // as they were sent instead of as they now are.
                const auto list = accounts.List();
                json updated = json{{"username", username}};
                if (list.has_value()) {
                    for (const auto& account : *list) {
                        if (account.username == username) {
                            updated = AccountToJson(account);
                            break;
                        }
                    }
                }
                res.status = 200;
                res.set_content(updated.dump(), "application/json");
                return;
            }
            case PatchAccountResult::kNotFound:
                SendError(res, 404, "not_found", "no account with that username",
                          req.get_header_value("X-Request-Id"));
                return;
            case PatchAccountResult::kError:
                SendError(res, 500, "internal_error", "could not update account",
                          req.get_header_value("X-Request-Id"));
                return;
        }
    });
}

}  // namespace srs