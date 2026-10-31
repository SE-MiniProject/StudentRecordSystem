#pragma once

#include <string>

namespace httplib {
class Server;
}

namespace srs {

// Where the web client lives, relative to the process working directory, and
// the environment variable that overrides it.
//
// The server and the client are one deliverable: the C++ process answers the
// REST API *and* serves the browser application, so the frontend is same-origin
// and no CORS configuration exists to get wrong. This replaced a Qt desktop
// client that shipped as a second binary.
constexpr const char* kWebRootEnvVar = "SRS_WEB_ROOT";
constexpr const char* kDefaultWebRoot = "web";

// Resolves the directory to serve from: $SRS_WEB_ROOT if set and non-empty,
// otherwise kDefaultWebRoot. Returned as-is; use WebRootExists to check it.
std::string WebRootFromEnv();

// True when `root` is an existing directory. Missing assets are a deployment
// error worth reporting at startup rather than a 404 a user discovers, so
// RunServer logs this and keeps serving the API.
bool WebRootExists(const std::string& root);

// Serves the web client.
//
// GET /            -> <root>/index.html
// GET /<path>      -> <root>/<path>
//
// Deliberately explicit rather than httplib's set_mount_point: a mount point is
// a catch-all that also swallows unknown /api paths, turning a JSON 404 from
// the API into an HTML one. The routes below only claim the two shapes the
// client needs, and RegisterRoutes installs them after every API route so an
// API path can never be shadowed.
void RegisterStaticRoutes(httplib::Server& server, const std::string& root);

}  // namespace srs