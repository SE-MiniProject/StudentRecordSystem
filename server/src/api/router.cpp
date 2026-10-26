#include "api/router.h"

#include <string>

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
void RegisterErrorHandler(httplib::Server& server) {
    server.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        const char* code = "error";
        std::string message = httplib::status_message(res.status);

        if (res.status == 404) {
            code = "not_found";
            message = "no route for " + req.method + " " + req.path;
        }

        res.set_content(
            nlohmann::json{
                {"error", code},
                {"message", message},
                {"requestId", req.get_header_value("X-Request-Id")},
            }
                .dump(),
            "application/json");
    });
}

}  // namespace

void RegisterRoutes(httplib::Server& server) {
    RegisterHealth(server);
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