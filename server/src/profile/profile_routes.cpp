#include "profile/profile_routes.h"

#include "api/error_response.h"
#include "auth/rbac.h"
#include "auth/session_store.h"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "profile/profile_store.h"

namespace srs {

void RegisterProfileRoutes(httplib::Server& server, ProfileStore& profiles,
                           SessionStore& sessions) {
    const RbacGuard guard(sessions);

    server.Get("/api/me", [&profiles, guard](const httplib::Request& req, httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.Authenticate(req, &who), req, res)) {
            return;
        }

        // The username comes from the session, not the request, so a caller can
        // only ever read their own profile.
        const auto profile = profiles.Find(who.username);
        if (!profile.has_value()) {
            SendError(res, 404, "not_found", "no account for this session",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        nlohmann::json body{
            {"username", profile->username},
            {"fullName", profile->full_name},
            {"role", std::string(RoleName(who.role))},
            // null rather than "" for non-students, so a client cannot mistake an
            // empty string for a roll number and request /api/grades/.
            {"rollNo", profile->roll_no.empty() ? nlohmann::json(nullptr)
                                                : nlohmann::json(profile->roll_no)},
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });
}

}  // namespace srs
