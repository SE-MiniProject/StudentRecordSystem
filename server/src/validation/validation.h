#pragma once

#include <string>
#include <vector>

namespace srs::validation {

// Server-side field validation, per SR-003. Every rule here is a pure function
// of its input: no clock, no database, no request object. That is what lets the
// rules be tested exhaustively and shared by every route, so a rule cannot be
// stricter on one endpoint than another.
//
// The server never trusts the client's copy of these rules. The Qt dialog runs
// the same checks for immediate feedback, but a request that bypasses the UI
// (curl, a script, a stale client) is validated again here, and this is the copy
// that decides.
namespace detail {

// Canonical lowercase hex for the 64-character digest the contract specifies.
bool IsSha256Hex(const std::string& value);

// A YYYY-MM-DD date that exists on the calendar. Rejects 2026-02-30 and
// 2025-02-29; month lengths and leap years are checked, not just the shape.
bool IsCalendarDate(const std::string& value);

}  // namespace detail

// The result of validating one request body. `ok` is true only when every rule
// passed. On failure, `fields` maps field name to the reason it was rejected,
// which is exactly the object the 400 envelope carries under "fields".
//
// A map is not used because the order of reported failures should be the order
// the fields appear in the contract, and because a vector keeps the reason next
// to its field without a second lookup.
struct FieldErrors {
    struct FieldError {
        std::string field;
        std::string problem;
    };

    std::vector<FieldError> errors;

    bool ok() const { return errors.empty(); }
    void Add(const std::string& field, const std::string& problem) {
        errors.push_back({field, problem});
    }
};

// The student fields the contract defines, already extracted from JSON. Absent
// optional fields are empty strings or the sentinel below; validation never sees
// a JSON value, so the rules are testable without building a request.
//
// `semester` is an int with a sentinel rather than std::optional<int> because
// the sentinel is also what a JSON value of the wrong type (a string, a float)
// decodes to, and both are reported the same way: "must be an integer 1-8".
struct StudentFields {
    static constexpr int kAbsentSemester = -1;

    std::string roll_no;
    std::string full_name;
    std::string date_of_birth;
    std::string department;
    std::string contact;
    int semester = kAbsentSemester;
};

// Rules for POST /api/students, where every field is required.
FieldErrors ValidateStudentCreate(const StudentFields& fields);

// Rules for PUT /api/students/{rollNo}. `roll_no` is immutable and is not
// validated here; the remaining fields are optional, and an empty string means
// "leave unchanged" rather than "blank out", matching the contract's
// partial-update semantics.
FieldErrors ValidateStudentUpdate(const StudentFields& fields);

// Rules for a grade. 0.0-10.0 inclusive is the contract's range (F-012).
bool IsValidGrade(double grade);

// Rules for an attendance date: a real calendar date, not in the future.
// `today` is YYYY-MM-DD and is passed in rather than read from the clock so the
// rule is testable and the route owns the notion of "now".
bool IsValidAttendanceDate(const std::string& date, const std::string& today);

// Rules for an attendance status. The contract allows exactly two values.
bool IsValidAttendanceStatus(const std::string& status);

}  // namespace srs::validation
