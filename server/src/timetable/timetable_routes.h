#pragma once

namespace httplib {
class Server;
}

namespace srs {

class ProfileStore;
class SessionStore;
class StudentStore;
class TimetableStore;

// SRS-F-020 (AC-020 / TC-020): role-scoped timetable, enforced server-side.
//
// GET /api/timetable — "my schedule", scoped by who is asking:
//   Admin    every slot; optional ?teacher= ?department= ?semester= filters
//   Teacher  only slots they teach. ?teacher= naming anyone else is 403.
//   Student  only their own class (their department + semester). ?teacher=,
//            or a ?department= / ?semester= other than their own, is 403.
//
// GET /api/timetable/{rollNo} — one student's class timetable:
//   Admin any student (404 if unknown); Student only their own roll number;
//   everyone else 403. A Teacher reads their own schedule from the route above,
//   not another user's.
void RegisterTimetableRoutes(httplib::Server& server, TimetableStore& timetable,
                             ProfileStore& profiles, StudentStore& students,
                             SessionStore& sessions);

}  // namespace srs
