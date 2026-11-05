#pragma once

#include <string>

#include "auth/rbac.h"

namespace httplib {
class Request;
class Response;
}  // namespace httplib

namespace srs {

class StudentStore;

// Shared access rules for the per-student record routes added in Sprint 2:
// GET /api/grades/{rollNo} (SRS-F-014) and GET /api/attendance/{rollNo}
// (F-017/F-018). Both have the same contract line, "Admin, Teacher,
// Student (self)", and both must answer it identically:
//
//   * no / bad token                      -> 401
//   * a Student asking for another roll   -> 403 (decided here, on the server)
//   * Admin or Teacher, unknown roll      -> 404
//   * otherwise                           -> allowed, `who` filled in
//
// Kept in one place because two copies of "who may read this student" is how
// one route ends up leaking what the other one guards.
//
// Writes the error response itself and returns false when the request must
// stop, so a route reads:
//
//   Principal who;
//   if (!AllowStudentRecordRead(guard, students, req, res, roll_no, &who)) return;
bool AllowStudentRecordRead(const RbacGuard& guard, StudentStore& students,
                            const httplib::Request& req, httplib::Response& res,
                            const std::string& roll_no, Principal* who);

// The teacher filter a record query must apply for `who`: the teacher's own
// username, so the store returns only rows for subjects assigned to them in
// teacher_subjects, or an empty string for Admin / the owning Student, meaning
// "no subject filter". The filter is derived from the server-resolved
// principal, never from anything the client sent.
std::string TeacherScopeFor(const Principal& who);

// Reads the optional `semester` query parameter (F-023). Absent -> 0, meaning
// "all semesters". Present but not an integer in 1..12 -> writes a 400
// validation_failed with fields.semester and returns false; a typo must not
// silently widen the result to every semester.
bool ReadSemesterParam(const httplib::Request& req, httplib::Response& res, int* semester);

// The roll number captured by a "/api/<thing>/([^/]+)" route pattern.
std::string PathParam(const httplib::Request& req);

}  // namespace srs
