#include "records/record_access.h"

#include <string>

#include "api/error_response.h"
#include "httplib.h"
#include "students/student_store.h"

namespace srs {

bool AllowStudentRecordRead(const RbacGuard& guard, StudentStore& students,
                            const httplib::Request& req, httplib::Response& res,
                            const std::string& roll_no, Principal* who) {
    // The owner is looked up before the guard because the guard compares it to
    // the caller. An unknown roll number has no owner, so a Student asking for
    // it fails the ownership check and gets 403, never 404: the response must
    // not tell a student which roll numbers exist.
    const std::string owner = students.OwnerOf(roll_no);

    if (!GuardAllows(guard.RequireRoleOrOwnership(req, {Role::kAdmin, Role::kTeacher}, owner, who),
                     req, res)) {
        return false;
    }

    // Defence in depth: RequireRoleOrOwnership already refuses a Student who is
    // not the owner. This repeats the rule in the one place it matters most, so
    // a future change to the guard's semantics cannot quietly open grades.
    if (who->role == Role::kStudent && (owner.empty() || owner != who->username)) {
        SendError(res, 403, "forbidden", "students may only read their own records",
                  req.get_header_value("X-Request-Id"));
        return false;
    }

    if (!students.Find(roll_no).has_value()) {
        SendError(res, 404, "not_found", "no student with that roll number",
                  req.get_header_value("X-Request-Id"));
        return false;
    }
    return true;
}

std::string TeacherScopeFor(const Principal& who) {
    return who.role == Role::kTeacher ? who.username : std::string();
}

bool ReadSemesterParam(const httplib::Request& req, httplib::Response& res, int* semester) {
    *semester = 0;
    if (!req.has_param("semester")) return true;

    const std::string raw = req.get_param_value("semester");
    int value = 0;
    std::size_t used = 0;
    try {
        value = std::stoi(raw, &used);
    } catch (const std::exception&) {
        used = 0;
    }
    if (raw.empty() || used != raw.size() || value < 1 || value > 12) {
        SendError(res, 400, "validation_failed", "semester must be an integer from 1 to 12",
                  req.get_header_value("X-Request-Id"), "semester", "out of range");
        return false;
    }
    *semester = value;
    return true;
}

std::string PathParam(const httplib::Request& req) { return req.matches[1].str(); }

}  // namespace srs
