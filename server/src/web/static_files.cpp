#include "web/static_files.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "api/error_response.h"
#include "httplib.h"

namespace srs {
namespace {

namespace fs = std::filesystem;

// Extensions the client actually ships. Anything else is refused rather than
// guessed at: a wrong Content-Type on a .js file makes the browser refuse to
// execute it, and `application/octet-stream` on index.html makes it download.
const char* MimeTypeFor(const std::string& extension) {
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js" || extension == ".mjs") return "text/javascript; charset=utf-8";
    if (extension == ".json") return "application/json; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".ico") return "image/x-icon";
    if (extension == ".woff2") return "font/woff2";
    return nullptr;
}

// Reads `candidate` only if, after resolving `.` and `..`, it is still inside
// `root`. This is the one check that stops `GET /../../etc/passwd`; percent
// decoding has already happened by the time httplib hands us req.path, so a
// naive join would happily serve anything the process can read.
bool ReadWithinRoot(const fs::path& root, const fs::path& candidate, std::string* body) {
    std::error_code ec;
    const fs::path resolved_root = fs::weakly_canonical(root, ec);
    if (ec) return false;

    const fs::path resolved = fs::weakly_canonical(candidate, ec);
    if (ec) return false;

    // Compare path components, not string prefixes: "/srv/web" must not be
    // treated as containing "/srv/web-backup".
    auto root_it = resolved_root.begin();
    auto path_it = resolved.begin();
    for (; root_it != resolved_root.end(); ++root_it, ++path_it) {
        if (path_it == resolved.end() || *root_it != *path_it) return false;
    }

    if (!fs::is_regular_file(resolved, ec) || ec) return false;

    std::ifstream in(resolved, std::ios::binary);
    if (!in) return false;

    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) return false;

    *body = std::move(data);
    return true;
}

// Serves one file, or writes the standard JSON 404 envelope. The envelope
// matters: a browser asking for a missing asset and a client asking for a
// missing route should both get a body that says what was missing, and the
// error handler in router.cpp must not overwrite it (it skips non-empty bodies).
void ServeFile(const fs::path& root, const std::string& relative, httplib::Response& res) {
    const fs::path candidate = root / fs::path(relative);
    std::string body;

    // httplib leaves the status at 200 when a handler returns without writing,
    // so every refusal has to set the status itself. `res.body` stays empty on
    // purpose: the global error handler in router.cpp fills in the JSON envelope
    // for a bodyless >=400 response, and a handler that wrote its own body would
    // suppress that.
    if (!ReadWithinRoot(root, candidate, &body)) {
        res.status = 404;
        return;
    }

    const char* mime = MimeTypeFor(candidate.extension().string());
    if (mime == nullptr) {
        res.status = 404;  // unsupported type: refused, not guessed
        return;
    }

    res.set_content(body, mime);
}

}  // namespace

std::string WebRootFromEnv() {
    const char* raw = std::getenv(kWebRootEnvVar);
    if (raw != nullptr && *raw != '\0') {
        return raw;
    }
    return kDefaultWebRoot;
}

bool WebRootExists(const std::string& root) {
    std::error_code ec;
    return fs::is_directory(root, ec) && !ec;
}

void RegisterStaticRoutes(httplib::Server& server, const std::string& root) {
    const fs::path root_path(root);

    server.Get("/", [root_path](const httplib::Request&, httplib::Response& res) {
        ServeFile(root_path, "index.html", res);
    });

    // Asset routes are registered by prefix, not with one catch-all pattern.
    //
    // httplib dispatches by walking get_handlers_ in registration order and
    // taking the first match, and it does not re-sort. A blanket `/(.+)` would
    // therefore swallow *every* later GET — including routes a test or a future
    // feature registers after RegisterRoutes, and any /api path that has no
    // handler. Those must fall through to the error handler and get the JSON
    // 404 envelope, not an HTML/asset 404.
    //
    // The consequence is that the set of served asset directories is explicit:
    // a new top-level directory under web/ needs a line here. That is a
    // deliberate trade — the alternative is a silent shadowing bug that only
    // shows up as "my new route returns 200 with an empty body".
    const char* const kAssetDirs[] = {"js/", "css/"};
    for (const char* dir : kAssetDirs) {
        const std::string prefix = std::string("/") + dir;
        // Trailing (.+) rather than (.*): a request for the bare directory
        // should 404, not resolve to the directory itself.
        const std::string subdir(dir);
        server.Get(prefix + "(.+)", [root_path, prefix, subdir](const httplib::Request& req,
                                                                httplib::Response& res) {
            if (req.path.compare(0, prefix.size(), prefix) != 0) {
                res.status = 404;
                return;
            }
            // Contain against the route's subdirectory, not the web root.
            //
            // Passing the raw path to ServeFile(root, ...) resolved `..` against
            // the root, so `/js/../package.json` was judged "inside the web
            // root" and served -- a file outside the /js/ subtree, reached
            // through a route that only claims /js/. Checking containment
            // against root/js/ makes the prefix a real boundary instead of a
            // string comparison that traversal walks straight through.
            const fs::path sub_root = root_path / subdir;
            ServeFile(sub_root, req.path.substr(prefix.size()), res);
        });
    }

    // Root-level files the client ships, by exact name. Kept explicit for the
    // same reason as the directories above: no pattern here may be broad enough
    // to claim a path somebody else owns.
    const char* const kRootFiles[] = {"favicon.svg", "index.html"};
    for (const char* file : kRootFiles) {
        server.Get(std::string("/") + file,
                   [root_path, file](const httplib::Request&, httplib::Response& res) {
                       ServeFile(root_path, file, res);
                   });
    }
}

}  // namespace srs