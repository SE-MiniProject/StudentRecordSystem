#pragma once

namespace httplib {
class Server;
}

namespace srs {

class SessionStore;
class StudentStore;

// Registers the student CRUD routes (SRS-16, SRS-17, SRS-26) and the
// account routes (SRS-17, SRS-19) on `server`.
//
// Both stores are parameters for the same reason RegisterLoginRoute takes one:
// the route's job is request parsing, response shape and status codes, and the
// tests drive those with fakes while the running server gets the Postgres
// implementations from RegisterRoutes.
//
// `sessions` backs the RBAC guard. It is not optional: a route that cannot
// resolve a token cannot decide whether the caller may use it.
void RegisterStudentRoutes(httplib::Server& server, StudentStore& students,
                           SessionStore& sessions);

}  // namespace srs