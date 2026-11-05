#pragma once

#include <optional>
#include <string>
#include <vector>

namespace srs {

// One grade as GET /api/grades/{rollNo} returns it (api.md "Grades").
struct GradeItem {
    std::string course_code;
    std::string course_name;  // subjects.name, or the code when the subject is unknown
    double grade = 0.0;       // 0.0-10.0, one decimal (NUMERIC(3,1))
    int semester = 0;         // subjects.semester; 0 when the subject is unknown
    std::string updated_at;   // ISO 8601 UTC
};

// What one grades read is scoped to. Built by the route from the
// server-resolved principal, never from the request body.
struct GradeQuery {
    std::string roll_no;
    int semester = 0;              // 0 = all semesters (F-023)
    // Non-empty for a Teacher: only grades in subjects this username is
    // assigned to in teacher_subjects are returned ("Teacher sees own
    // subjects"). Empty for Admin and the owning Student: no subject filter.
    std::string teacher_username;
};

// Storage behind the grade read route. BE lane owns this interface; DB lane owns
// the query behind it.
class GradeStore {
public:
    virtual ~GradeStore() = default;

    // Grades for one student, filtered by `query`. nullopt only on a storage
    // failure; a student with no visible grades is an empty vector.
    virtual std::optional<std::vector<GradeItem>> ForStudent(const GradeQuery& query) = 0;
};

GradeStore& DefaultGradeStore();

}  // namespace srs
