#pragma once

namespace httplib {
class Server;
}

namespace srs {

class ProfileStore;
class SessionStore;

// GET /api/me (any authenticated role): the caller's own username, full name,
// role and, for a Student, linked roll number. Added for SRS-F-014 so the
// client can show a Student their own grades without asking for a roll number.
void RegisterProfileRoutes(httplib::Server& server, ProfileStore& profiles,
                           SessionStore& sessions);

}  // namespace srs
