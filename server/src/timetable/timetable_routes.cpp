#include "timetable/timetable_routes.h"

#include <optional>
#include <string>
#include <cctype>

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

std::string StringField(const json& body, const char* key) {
    return body.is_object() && body.contains(key) && body[key].is_string()
               ? body[key].get<std::string>()
               : std::string();
}

bool HasText(const std::string& value) {
    for (const unsigned char ch : value) {
        if (!std::isspace(ch)) return true;
    }
    return false;
}

bool ValidTime(const std::string& value) {
    if (value.size() != 5 || value[2] != ':') return false;
    if (!std::isdigit(static_cast<unsigned char>(value[0])) ||
        !std::isdigit(static_cast<unsigned char>(value[1])) ||
        !std::isdigit(static_cast<unsigned char>(value[3])) ||
        !std::isdigit(static_cast<unsigned char>(value[4]))) return false;
    const int hour = (value[0] - '0') * 10 + value[1] - '0';
    const int minute = (value[3] - '0') * 10 + value[4] - '0';
    return hour < 24 && minute < 60;
}

void SendSlotFieldErrors(httplib::Response& res, const httplib::Request& req,
                         const json& fields) {
    res.status = 400;
    res.set_content(json{{"error", {{"code", "validation_failed"},
                                     {"message", "one or more fields are invalid"},
                                     {"fields", fields},
                                     {"requestId", req.get_header_value("X-Request-Id")}}}}
                        .dump(), "application/json");
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

    // POST /api/timetable — Admin creates a weekly slot (SRS-F-019/F-021).
    server.Post("/api/timetable", [&timetable, guard](const httplib::Request& req,
                                                       httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kAdmin}, &who), req, res)) return;

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            SendError(res, 400, "invalid_json", "request body must be valid JSON",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        TimetableSlot slot;
        slot.course_code = StringField(body, "courseCode");
        slot.teacher_username = StringField(body, "teacher");
        slot.department = StringField(body, "department");
        slot.room = StringField(body, "room");
        slot.start_time = StringField(body, "startTime");
        slot.end_time = StringField(body, "endTime");
        if (body.is_object() && body.contains("dayOfWeek") && body["dayOfWeek"].is_number_integer())
            slot.day_of_week = body["dayOfWeek"].get<int>();
        if (body.is_object() && body.contains("semester") && body["semester"].is_number_integer())
            slot.semester = body["semester"].get<int>();

        json fields = json::object();
        if (!HasText(slot.course_code) || slot.course_code.size() > 32)
            fields["courseCode"] = "must be 1-32 non-blank characters";
        if (slot.teacher_username.size() < 3 || slot.teacher_username.size() > 64)
            fields["teacher"] = "must be a valid teacher username";
        if (!HasText(slot.department) || slot.department.size() > 100)
            fields["department"] = "must be 1-100 non-blank characters";
        if (!HasText(slot.room) || slot.room.size() > 32)
            fields["room"] = "must be 1-32 non-blank characters";
        if (slot.day_of_week < 1 || slot.day_of_week > 7)
            fields["dayOfWeek"] = "must be an ISO weekday from 1 to 7";
        if (slot.semester < 1 || slot.semester > 12)
            fields["semester"] = "must be an integer from 1 to 12";
        if (!ValidTime(slot.start_time)) fields["startTime"] = "must be a valid HH:MM time";
        if (!ValidTime(slot.end_time)) fields["endTime"] = "must be a valid HH:MM time";
        if (ValidTime(slot.start_time) && ValidTime(slot.end_time) &&
            slot.start_time >= slot.end_time)
            fields["endTime"] = "must be later than startTime";
        if (!fields.empty()) {
            SendSlotFieldErrors(res, req, fields);
            return;
        }

        TimetableSlot created;
        switch (timetable.Create(slot, &created)) {
            case CreateTimetableResult::kCreated:
                res.status = 201;
                res.set_content(SlotToJson(created).dump(), "application/json");
                return;
            case CreateTimetableResult::kConflict:
                SendError(res, 409, "timetable_conflict",
                          "the teacher or room is already booked during this time",
                          req.get_header_value("X-Request-Id"));
                return;
            case CreateTimetableResult::kInvalidAssignment:
                SendError(res, 400, "invalid_assignment",
                          "the teacher must be active and assigned to this course, and the course semester must match",
                          req.get_header_value("X-Request-Id"), "teacher", "not assigned to course");
                return;
            case CreateTimetableResult::kError:
                SendError(res, 500, "internal", "could not create timetable slot",
                          req.get_header_value("X-Request-Id"));
                return;
        }
    });

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
