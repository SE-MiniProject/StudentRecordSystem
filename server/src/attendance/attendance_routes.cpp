#include "attendance/attendance_routes.h"

#include <ctime>
#include <utility>
#include <string>

#include "api/error_response.h"
#include "attendance/attendance_math.h"
#include "attendance/attendance_store.h"
#include "auth/rbac.h"
#include "auth/session_store.h"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "records/record_access.h"
#include "students/student_store.h"
#include "validation/validation.h"

namespace srs {
namespace {

using json = nlohmann::json;

json PercentageJson(long long present, long long conducted) {
    const auto pct = attendance::Percentage(present, conducted);
    return pct.has_value() ? json(*pct) : json(nullptr);
}

std::string Today() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char value[11]{};
    std::strftime(value, sizeof(value), "%Y-%m-%d", &local);
    return value;
}

bool ReadEntry(const json& body, AttendanceEntry* entry, std::string* status) {
    if (!body.is_object()) return false;
    if (!body.contains("rollNo") || !body["rollNo"].is_string() ||
        !body.contains("courseCode") || !body["courseCode"].is_string() ||
        !body.contains("sessionDate") || !body["sessionDate"].is_string() ||
        !body.contains("status") || !body["status"].is_string()) return false;
    entry->roll_no = body["rollNo"].get<std::string>();
    entry->course_code = body["courseCode"].get<std::string>();
    entry->session_date = body["sessionDate"].get<std::string>();
    *status = body["status"].get<std::string>();
    return true;
}

void WriteAttendance(AttendanceStore& attendance, StudentStore& students,
                     const httplib::Request& req, httplib::Response& res,
                     const Principal& who, AttendanceEntry entry, const std::string& status,
                     bool update) {
    const std::string request_id = req.get_header_value("X-Request-Id");
    if (entry.roll_no.empty() || entry.roll_no.size() > 32 || entry.course_code.empty() ||
        entry.course_code.size() > 32 || !validation::IsValidAttendanceDate(entry.session_date, Today()) ||
        !validation::IsValidAttendanceStatus(status)) {
        SendError(res, 400, "validation_failed", "invalid attendance fields", request_id,
                  "attendance", "rollNo and courseCode are required; date must be a valid date not in the future; status must be present or absent");
        return;
    }
    if (!students.Find(entry.roll_no).has_value()) {
        SendError(res, 404, "not_found", "no student with that roll number", request_id);
        return;
    }
    entry.present = status == "present";
    const auto result = attendance.Write(entry, who.username, update);
    switch (result) {
        case AttendanceWriteResult::kOk:
            res.status = update ? 200 : 201;
            res.set_content(json{{"rollNo", entry.roll_no}, {"courseCode", entry.course_code},
                                 {"sessionDate", entry.session_date}, {"status", status}}
                                .dump(), "application/json");
            return;
        case AttendanceWriteResult::kDuplicate:
            SendError(res, 409, "conflict", "attendance already exists for this student, course and date", request_id);
            return;
        case AttendanceWriteResult::kNotFound:
            SendError(res, 404, "not_found", "no attendance entry exists for this student, course and date", request_id);
            return;
        case AttendanceWriteResult::kForbidden:
            SendError(res, 403, "forbidden", "you are not assigned to this course", request_id);
            return;
        case AttendanceWriteResult::kError:
            SendError(res, 500, "internal", "could not write attendance", request_id);
            return;
    }
}

}  // namespace

void RegisterAttendanceRoutes(httplib::Server& server, AttendanceStore& attendance,
                              StudentStore& students, SessionStore& sessions) {
    const RbacGuard guard(sessions);

    server.Post("/api/attendance", [&attendance, &students, guard](const httplib::Request& req,
                                                                    httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kTeacher}, &who), req, res)) return;
        json body;
        try { body = json::parse(req.body); } catch (...) {
            SendError(res, 400, "validation_failed", "request body must be valid JSON",
                      req.get_header_value("X-Request-Id"));
            return;
        }
        AttendanceEntry entry;
        std::string status;
        if (!ReadEntry(body, &entry, &status)) {
            SendError(res, 400, "validation_failed", "rollNo, courseCode, sessionDate and status are required",
                      req.get_header_value("X-Request-Id"));
            return;
        }
        WriteAttendance(attendance, students, req, res, who, std::move(entry), status, false);
    });

    server.Put(R"(/api/attendance/([^/]+)/([^/]+)/([^/]+))",
               [&attendance, &students, guard](const httplib::Request& req, httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kTeacher}, &who), req, res)) return;
        json body;
        try { body = json::parse(req.body); } catch (...) {
            SendError(res, 400, "validation_failed", "request body must be valid JSON",
                      req.get_header_value("X-Request-Id"));
            return;
        }
        AttendanceEntry entry{req.matches[1].str(), req.matches[2].str(), req.matches[3].str(), false};
        if (!body.is_object() || !body.contains("status") || !body["status"].is_string()) {
            SendError(res, 400, "validation_failed", "status is required", req.get_header_value("X-Request-Id"));
            return;
        }
        WriteAttendance(attendance, students, req, res, who, std::move(entry),
                        body["status"].get<std::string>(), true);
    });

    server.Get(R"(/api/attendance/([^/]+))", [&attendance, &students, guard](
                                                 const httplib::Request& req,
                                                 httplib::Response& res) {
        const std::string roll_no = PathParam(req);
        const std::string request_id = req.get_header_value("X-Request-Id");
        Principal who;
        if (!AllowStudentRecordRead(guard, students, req, res, roll_no, &who)) return;

        AttendanceQuery query;
        query.roll_no = roll_no;
        query.teacher_username = TeacherScopeFor(who);
        query.course_code = req.has_param("courseCode") ? req.get_param_value("courseCode") : std::string();
        if (!ReadSemesterParam(req, res, &query.semester)) return;

        const auto counts = attendance.CountsFor(query);
        if (!counts.has_value()) {
            SendError(res, 500, "internal", "could not read attendance", request_id);
            return;
        }
        json items = json::array();
        long long total_conducted = 0;
        long long total_present = 0;
        for (const auto& row : *counts) {
            if (attendance::CheckTally(row.present, row.conducted) != attendance::TallyCheck::kOk) {
                SendError(res, 500, "internal", "attendance counts are inconsistent for " + row.course_code + ": present exceeds conducted", request_id);
                return;
            }
            total_conducted += row.conducted;
            total_present += row.present;
            items.push_back(json{{"courseCode", row.course_code}, {"courseName", row.course_name},
                                 {"semester", row.semester}, {"conducted", row.conducted},
                                 {"present", row.present}, {"percentage", PercentageJson(row.present, row.conducted)}});
        }
        res.status = 200;
        res.set_content(json{{"studentRollNo", roll_no}, {"items", items},
                             {"overall", {{"conducted", total_conducted}, {"present", total_present},
                                          {"percentage", PercentageJson(total_present, total_conducted)}}}}
                            .dump(), "application/json");
    });
}

}  // namespace srs
