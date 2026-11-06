#pragma once

#include <optional>
#include <string>
#include <vector>

namespace srs {

struct AttendanceCount {
    std::string course_code;
    std::string course_name;
    int semester = 0;
    long long conducted = 0;
    long long present = 0;
};

struct AttendanceQuery {
    std::string roll_no;
    int semester = 0;
    std::string course_code;
    std::string teacher_username;
};

struct AttendanceEntry {
    std::string roll_no;
    std::string course_code;
    std::string session_date;
    bool present = false;
};

enum class AttendanceWriteResult { kOk, kDuplicate, kNotFound, kForbidden, kError };

class AttendanceStore {
public:
    virtual ~AttendanceStore() = default;
    virtual std::optional<std::vector<AttendanceCount>> CountsFor(const AttendanceQuery& query) = 0;
    // Teacher assignment is checked by the store against teacher_subjects in the same
    // transaction as the write. `update` never inserts; `false` never overwrites.
    virtual AttendanceWriteResult Write(const AttendanceEntry& entry,
                                        const std::string& teacher_username, bool update) = 0;
};

AttendanceStore& DefaultAttendanceStore();

}  // namespace srs
