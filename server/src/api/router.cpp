#include "api/router.h"

#include <string>

#include "api/error_response.h"
#include "auth/login_route.h"
#include "auth/logout_route.h"
#include "auth/session_store.h"
#include "auth/user_store.h"
#include "httplib.h"
#include "nlohmann/json.hpp"

namespace srs {
namespace {

constexpr const char* kServiceName = "srs_server";
constexpr const char* kVersion = "0.1.0";

nlohmann::json HealthPayload() {
    return nlohmann::json{
        {"status", "ok"},
        {"service", kServiceName},
        {"version", kVersion},
    };
}

void RegisterHealth(httplib::Server& server) {
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(HealthPayload().dump(), "application/json");
    });
}

// Every error response carries the same JSON envelope, so a client never has to
// parse two formats. A request for a path that exists under a different verb
// says which verb was used, which is the difference between "wrong URL" and
// "right URL, wrong method" when debugging a client.
//
// httplib calls this handler for *every* response with status >= 400, not just
// for unrouted ones. A route that already wrote a body (login's 400/401) has a
// more specific error than the generic one here, so this returns Unhandled and
// leaves that body alone. Returning void would make httplib treat every 4xx as
// "handled" and overwrite the route's JSON with {"error":"error"}.
void RegisterErrorHandler(httplib::Server& server) {
    server.set_error_handler(
        [](const httplib::Request& req, httplib::Response& res) -> httplib::Server::HandlerResponse {
            if (!res.body.empty()) {
                return httplib::Server::HandlerResponse::Unhandled;
            }

            std::string code = "error";
            std::string message = httplib::status_message(res.status);

            if (res.status == 404) {
                code = "not_found";
                message = "no route for " + req.method + " " + req.path;
            }

            SendError(res, res.status, code, message, req.get_header_value("X-Request-Id"));

            return httplib::Server::HandlerResponse::Handled;
        });
}

}  // namespace

void RegisterRoutes(httplib::Server& server) {
    RegisterHealth(server);
    // The default store rejects every login until the users table is wired up.
    // Tests call RegisterLoginRoute directly with a fake.
    RegisterLoginRoute(server, DefaultUserStore());
    // Likewise rejects every token until the sessions table is wired up.
    RegisterLogoutRoute(server, DefaultSessionStore());
    RegisterErrorHandler(server);
}

int RunServer(int port) {
    httplib::Server server;

    RegisterRoutes(server);

    if (!server.listen("0.0.0.0", port)) {
        return 1;
    }
    return 0;
}

}  // namespace srs