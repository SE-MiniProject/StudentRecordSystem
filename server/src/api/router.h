#pragma once

namespace httplib {
class Server;
}

namespace srs {

constexpr int kDefaultPort = 8088;

// Registers every route on the given server. Separate from RunServer so tests
// can build a server, bind it to an ephemeral port, and make real requests
// without spawning a process.
void RegisterRoutes(httplib::Server& server);

// Binds the routes to a server and blocks until the listener stops.
// Returns the process exit code. httplib.h stays inside router.cpp so it is
// compiled once rather than in every translation unit.
int RunServer(int port);

}  // namespace srs