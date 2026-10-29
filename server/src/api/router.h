#pragma once

#include <string>

namespace httplib {
class Server;
}

namespace srs {

constexpr int kDefaultPort = 8088;

// Registers every route on the given server. Separate from RunServer so tests
// can build a server, bind it to an ephemeral port, and make real requests
// without spawning a process. This is the single route table: the executable and
// every test go through it, so a route cannot be tested without also being
// reachable in production.
void RegisterRoutes(httplib::Server& server);

// Binds the routes to a server and blocks until the listener stops.
// Returns the process exit code. httplib.h stays inside router.cpp so it is
// compiled once rather than in every translation unit.
int RunServer(int port, const std::string& bind);

}  // namespace srs