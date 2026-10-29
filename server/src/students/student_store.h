#pragma once

#include <optional>
#include <string>
#include <vector>

namespace srs {

// One student, in the wire contract's spelling (api.md "Students"). Field names
// are camelCase because this type crosses the API boundary unchanged; the
// database's snake_case columns are translated inside the store, not here.
struct Student {
    std::string roll_no;      // primary key, immutable
    std::string full_name;
    std::string date_of_birth;  // YYYY-MM-DD
    std::string department;
    std::string contact;
    int semester = 0;
};

// A page of students plus the total matching the filter, so the client can size
// a view without a second request (api.md "Pagination").
struct StudentPage {
    std::vector<Student> items;
    long long total = 0;
    int limit = 50;
    int offset = 0;
};

// What GET /api/students accepts as a filter.
struct StudentQuery {
    std::string search;   // partial name or roll number
    int semester = 0;     // 0 = no semester filter
    int limit = 50;
    int offset = 0;
};

// The outcome of a create, as the store reports it. Duplicate roll numbers are a
// distinct outcome because the contract answers 409 rather than 400, and only
// the store can tell the difference between a conflict and a bad value.
enum class CreateStudentResult {
    kOk,
    kDuplicateRollNo,
    kError,
};

// The outcome of an update. kNotFound is separate from kError so the route can
// answer 404 without a pre-read that would race with a concurrent delete.
enum class UpdateStudentResult {
    kOk,
    kNotFound,
    kError,
};

// Storage behind the student CRUD routes. An interface for the same reason
// UserStore is one: the route's job is request parsing, response shape and
// status codes, and it must be testable against a fake without a database.
//
// Ownership: BE lane owns this interface; DB lane owns what backs it.
class StudentStore {
public:
    virtual ~StudentStore() = default;

    // One page of students matching `query`, plus the unfiltered-by-page total.
    virtual std::optional<StudentPage> List(const StudentQuery& query) = 0;

    // A single student by roll number, or nullopt when absent.
    virtual std::optional<Student> Find(const std::string& roll_no) = 0;

    // The username of the account linked to `roll_no`, or empty when no account
    // is linked. Used by the ownership check on GET /api/students/{rollNo}:
    // a student may read their own record and nothing else.
    virtual std::string OwnerOf(const std::string& roll_no) = 0;

    virtual CreateStudentResult Create(const Student& student) = 0;

    // Partial update: the contract says omitted fields are unchanged, which is
    // expressed here as empty strings / a zero semester meaning "leave alone".
    virtual UpdateStudentResult Update(const std::string& roll_no, const Student& changes) = 0;

    // Hard delete, cascading grades and attendance (F-011). Returns false when
    // the roll number did not exist.
    virtual bool Delete(const std::string& roll_no) = 0;
};

// The store RegisterRoutes uses when none is injected. Without DATABASE_URL it
// reports every operation as empty/failed, so the routes are reachable and
// correctly shaped without pretending to have data.
StudentStore& DefaultStudentStore();

}  // namespace srs