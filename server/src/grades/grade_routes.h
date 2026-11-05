#pragma once

namespace httplib {
class Server;
}

namespace srs {

class GradeStore;
class SessionStore;
class StudentStore;

// GET /api/grades/{rollNo} — SRS-F-014, AC-014 / TC-014.
//
//   Admin    every grade of any student
//   Teacher  only grades in subjects assigned to them (teacher_subjects)
//   Student  their own grades only; any other roll number is 403, decided here
//
// `students` resolves ownership and existence; `grades` runs the scoped query.
void RegisterGradeRoutes(httplib::Server& server, GradeStore& grades, StudentStore& students,
                         SessionStore& sessions);

}  // namespace srs
