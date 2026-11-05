#pragma once

#include <optional>
#include <string>
#include <vector>

namespace srs {

// Raw counts for one course, straight from the counts query. The percentage is
// deliberately not here: it is computed (and the counts validated) in one place,
// attendance_math, by the route.
struct AttendanceCount {
    std::string course_code;
    std::string course_name;  // subjects.name, or the code when unknown
    int semester = 0;         // subjects.semester; 0 when unknown
    long long conducted = 0;  // sessions recorded for this student and course
    long long present = 0;    // of those, sessions marked present
};

struct AttendanceQuery {
    std::string roll_no;
    int semester = 0;              // 0 = all (F-023)
    std::string course_code;       // '' = all courses
    // Non-empty for a Teacher: only courses assigned to this username in
    // teacher_subjects. Empty for Admin and the owning Student.
    std::string teacher_username;
};

// BE lane owns this interface; DB lane owns the counts query behind it.
class AttendanceStore {
public:
    virtual ~AttendanceStore() = default;

    // Per-course counts for one student. nullopt only on a storage failure.
    virtual std::optional<std::vector<AttendanceCount>> CountsFor(const AttendanceQuery& query) = 0;
};

AttendanceStore& DefaultAttendanceStore();

}  // namespace srs
