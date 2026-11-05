#pragma once

namespace httplib {
class Server;
}

namespace srs {

class AttendanceStore;
class SessionStore;
class StudentStore;

// GET /api/attendance/{rollNo} — SRS-F-017, F-018 (AC-017, AC-018 / TC-017,
// TC-018).
//
// Same access rule as grades (records/record_access.h): Admin any student,
// Teacher only their own subjects, Student only themselves (403 otherwise).
// The percentage is computed here, server-side, to two decimals; a count where
// present exceeds conducted is rejected rather than displayed.
void RegisterAttendanceRoutes(httplib::Server& server, AttendanceStore& attendance,
                              StudentStore& students, SessionStore& sessions);

}  // namespace srs
