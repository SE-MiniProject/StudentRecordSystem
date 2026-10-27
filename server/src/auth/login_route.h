#pragma once

namespace httplib {
class Server;
}

namespace srs {

class UserStore;

// Registers POST /api/auth/login on `server`, backed by `store`. Taking the
// store as a parameter is what lets the tests swap in a fake: the route's job
// is request parsing and response shape, and the storage policy is behind the
// interface.
void RegisterLoginRoute(httplib::Server& server, UserStore& store);

}  // namespace srs
