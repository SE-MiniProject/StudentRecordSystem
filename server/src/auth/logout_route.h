#pragma once

namespace httplib {
class Server;
}

namespace srs {

class SessionStore;

// Registers POST /api/auth/logout on `server`, backed by `store`.
//
// The route takes the store as a parameter for the same reason login does: its
// job is reading the Authorization header and choosing 204 vs 401, and the
// storage policy is behind the interface.
void RegisterLogoutRoute(httplib::Server& server, SessionStore& store);

}  // namespace srs