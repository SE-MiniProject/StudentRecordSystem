#include "auth/logout_route.h"

#include <string>

#include "api/error_response.h"
#include "auth/bearer_token.h"
#include "auth/session_store.h"
#include "httplib.h"

namespace srs {
namespace {

std::string RequestId(const httplib::Request& req) {
    return req.get_header_value("X-Request-Id");
}

void SendUnauthenticated(const httplib::Request& req, httplib::Response& res) {
    SendError(res, 401, "unauthenticated", "invalid or missing bearer token", RequestId(req));
}

}  // namespace

void RegisterLogoutRoute(httplib::Server& server, SessionStore& store) {
    server.Post("/api/auth/logout",
                [&store](const httplib::Request& req, httplib::Response& res) {
                    const auto token = BearerToken(req);
                    if (token.empty()) {
                        SendUnauthenticated(req, res);
                        return;
                    }

                    // Revoke both authenticates and invalidates in one step: it
                    // returns false exactly when the token was not a live
                    // session, which is the contract's 401. Doing it this way
                    // avoids a Resolve-then-Revoke pair that another logout
                    // could slip between.
                    if (!store.Revoke(token)) {
                        SendUnauthenticated(req, res);
                        return;
                    }

                    // 204: the contract says no body, and the client has nothing
                    // left to do with the token.
                    res.status = 204;
                });
}

}  // namespace srs