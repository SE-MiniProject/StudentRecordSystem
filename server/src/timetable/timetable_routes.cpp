#include "timetable/timetable_routes.h"

#include <optional>
#include <string>

#include "api/error_response.h"
#include "auth/rbac.h"
#include "auth/session_store.h"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "profile/profile_store.h"
#include "records/record_access.h"
#include "students/student_store.h"
#include "timetable/timetable_store.h"

namespace srs {
namespace {

using json = nlohmann::json;

json SlotToJson(const TimetableSlot& slot) {
    return json{
        {"id", slot.id},
        {"dayOfWeek", slot.day_of_week},
        {"startTime", slot.start_time},
        {"endTime", slot.end_time},
        {"courseCode", slot.course_code},
        {"courseName", slot.course_name},
        {"teacher", slot.teacher_username},
        {"teacherName", slot.teacher_name},
        {"department", slot.department},
        {"semester", slot.semester},
        {"room", slot.room},
    };
}

void SendSlots(httplib::Response& res, const httplib::Request& req, TimetableStore& timetable,
               const TimetableQuery& query, const std::string& scope_role) {
    const auto slots = timetable.List(query);
    if (!slots.has_value()) {
        SendError(res, 500, "internal", "could not read the timetable",
                  req.get_header_value("X-Request-Id"));
        return;
    }
    json items = json::array();
    for (const auto& slot : *slots) items.push_back(SlotToJson(slot));

    // `scope` echoes what the server actually filtered on, so the client can
    // label the view ("Your schedule", "Computer Science · Semester 5")
    // without re-deriving the rule.
    json scope{{"role", scope_role}};
    scope["teacher"] = query.teacher_username.empty() ? json(nullptr) : json(query.teacher_username);
    scope["department"] = query.department.empty() ? json(nullptr) : json(query.department);
    scope["semester"] = query.semester == 0 ? json(nullptr) : json(query.semester);

    res.status = 200;
    res.set_content(json{{"scope", scope}, {"items", items}}.dump(), "application/json");
}

void Forbid(httplib::Response& res, const httplib::Request& req, const std::string& why) {
    SendError(res, 403, "forbidden", why, req.get_header_value("X-Request-Id"));
}

std::string Param(const httplib::Request& req, const char* name) {
    return req.has_param(name) ? req.get_param_value(name) : std::string();
}

// The (department, semester) a Student's account belongs to, via the
// account's linked roll number. nullopt when the account has no linked student.
std::optional<Student> OwnClass(ProfileStore& profiles, StudentStore& students,
                                const std::string& username) {
    const auto profile = profiles.Find(username);
    if (!profile.has_value() || profile->roll_no.empty()) return std::nullopt;
    return students.Find(profile->roll_no);
}

}  // namespace

void RegisterTimetableRoutes(httplib::Server& server, TimetableStore& timetable,
                             ProfileStore& profiles, StudentStore& students,
                             SessionStore& sessions) {
    const RbacGuard guard(sessions);

    // GET /api/timetable — the caller's own schedule.
    server.Get("/api/timetable", [&timetable, &profiles, &students, guard](
                                     const httplib::Request& req, httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.Authenticate(req, &who), req, res)) {
            return;
        }

        int semester = 0;
        if (!ReadSemesterParam(req, res, &semester)) {
            return;
        }
        const std::string asked_teacher = Param(req, "teacher");
        const std::string asked_department = Param(req, "department");

        TimetableQuery query;
        switch (who.role) {
            case Role::kAdmin:
                query.teacher_username = asked_teacher;
                query.department = asked_department;
                query.semester = semester;
                break;

            case Role::kTeacher:
                // Cross-user fetch: naming another teacher is refused, not
                // silently replaced with the caller's own schedule, so a client
                // bug or a probe is visible as a 403.
                if (!asked_teacher.empty() && asked_teacher != who.username) {
                    Forbid(res, req, "teachers may only view their own schedule");
                    return;
                }
                query.teacher_username = who.username;
                query.department = asked_department;  // narrowing within own slots only
                query.semester = semester;
                break;

            case Role::kStudent: {
                const auto own = OwnClass(profiles, students, who.username);
                if (!own.has_value()) {
                    Forbid(res, req, "this account is not linked to a student record");
                    return;
                }
                const bool other_teacher = !asked_teacher.empty();
                const bool other_department =
                    !asked_department.empty() && asked_department != own->department;
                const bool other_semester = semester != 0 && semester != own->semester;
                if (other_teacher || other_department || other_semester) {
                    Forbid(res, req, "students may only view their own class schedule");
                    return;
                }
                query.department = own->department;
                query.semester = own->semester;
                break;
            }
        }

        SendSlots(res, req, timetable, query, std::string(RoleName(who.role)));
    });

    // GET /api/timetable/{rollNo} — one student's class timetable.
    server.Get(R"(/api/timetable/([^/]+))", [&timetable, &students, guard](
                                                const httplib::Request& req,
                                                httplib::Response& res) {
        const std::string roll_no = PathParam(req);
        const std::string owner = students.OwnerOf(roll_no);

        // Admin by role, or the student who owns the roll number. A Teacher is
        // neither, so asking for a student's schedule is a cross-user fetch: 403.
        Principal who;
        if (!GuardAllows(guard.RequireRoleOrOwnership(req, {Role::kAdmin}, owner, &who), req,
                         res)) {
            return;
        }
        if (who.role != Role::kAdmin && (owner.empty() || owner != who.username)) {
            Forbid(res, req, "role or ownership not permitted");
            return;
        }

        const auto student = students.Find(roll_no);
        if (!student.has_value()) {
            SendError(res, 404, "not_found", "no student with that roll number",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        TimetableQuery query;
        query.department = student->department;
        query.semester = student->semester;
        SendSlots(res, req, timetable, query, std::string(RoleName(who.role)));
    });
}

}  // namespace srs
