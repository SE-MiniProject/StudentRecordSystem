#include "grades/grade_routes.h"

#include <cmath>
#include <string>

#include "api/error_response.h"
#include "auth/rbac.h"
#include "auth/session_store.h"
#include "grades/grade_store.h"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "records/record_access.h"
#include "students/student_store.h"
#include "validation/validation.h"

namespace srs {
namespace {

using json = nlohmann::json;

json GradeToJson(const GradeItem& item) {
    return json{
        {"courseCode", item.course_code},
        {"courseName", item.course_name},
        // NUMERIC(3,1) arrives as a double; round to one decimal so 8.5 is never
        // sent as 8.4999999.
        {"grade", std::round(item.grade * 10.0) / 10.0},
        {"semester", item.semester},
        {"updatedAt", item.updated_at},
    };
}

std::string StringField(const json& body, const char* key) {
    if (!body.is_object() || !body.contains(key) || !body[key].is_string()) {
        return {};
    }
    return body[key].get<std::string>();
}

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}  // namespace

void RegisterGradeRoutes(httplib::Server& server, GradeStore& grades, StudentStore& students,
                         SessionStore& sessions) {
    const RbacGuard guard(sessions);

    // SRS-23 / SRS-F-012 / AC-012 / AC-013: only a Teacher may enter grades,
    // and the server derives the teacher identity from the authenticated session.
    server.Post("/api/grades", [&grades, guard](const httplib::Request& req,
                                                httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kTeacher}, &who), req, res)) return;

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            SendError(res, 400, "invalid_json", "request body must be valid JSON",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        const std::string roll_no = Trim(StringField(body, "studentRollNo"));
        const std::string subject_code = Trim(StringField(body, "courseCode"));
        if (roll_no.empty() || roll_no.size() > 32) {
            SendError(res, 400, "validation_failed",
                      "studentRollNo is required and must be at most 32 characters",
                      req.get_header_value("X-Request-Id"), "studentRollNo",
                      roll_no.empty() ? "required" : "too long");
            return;
        }
        if (subject_code.empty() || subject_code.size() > 32) {
            SendError(res, 400, "validation_failed",
                      "courseCode is required and must be at most 32 characters",
                      req.get_header_value("X-Request-Id"), "courseCode",
                      subject_code.empty() ? "required" : "too long");
            return;
        }
        if (!body.is_object() || !body.contains("grade") || !body["grade"].is_number()) {
            SendError(res, 400, "validation_failed",
                      "grade must be between 0.0 and 10.0 in 0.1 increments",
                      req.get_header_value("X-Request-Id"), "grade",
                      "must be between 0.0 and 10.0 in 0.1 increments");
            return;
        }
        const double grade = body["grade"].get<double>();
        const double tenths = grade * 10.0;
        const bool one_decimal_place =
            std::abs(tenths - std::round(tenths)) < 1e-9;
        if (!std::isfinite(grade) || !validation::IsValidGrade(grade) || !one_decimal_place) {
            SendError(res, 400, "validation_failed",
                      "grade must be between 0.0 and 10.0 in 0.1 increments",
                      req.get_header_value("X-Request-Id"), "grade",
                      "must be between 0.0 and 10.0 in 0.1 increments");
            return;
        }

        const GradeWrite write{roll_no, subject_code, who.username, grade};
        switch (grades.Upsert(write)) {
            case GradeWriteResult::kCreated:
                res.status = 201;
                res.set_content(json{{"studentRollNo", roll_no}, {"courseCode", subject_code},
                                     {"grade", grade}, {"created", true}}
                                    .dump(),
                                "application/json");
                return;
            case GradeWriteResult::kUpdated:
                res.status = 200;
                res.set_content(json{{"studentRollNo", roll_no}, {"courseCode", subject_code},
                                     {"grade", grade}, {"created", false}}
                                    .dump(),
                                "application/json");
                return;
            case GradeWriteResult::kStudentNotFound:
                SendError(res, 404, "not_found", "no student with that roll number",
                          req.get_header_value("X-Request-Id"));
                return;
            case GradeWriteResult::kTeacherNotAssigned:
                SendError(res, 403, "forbidden", "teacher is not assigned to this subject",
                          req.get_header_value("X-Request-Id"));
                return;
            case GradeWriteResult::kError:
                SendError(res, 500, "internal", "could not save grade",
                          req.get_header_value("X-Request-Id"));
                return;
        }
    });

    server.Get(R"(/api/grades/([^/]+))", [&grades, &students, guard](const httplib::Request& req,
                                                                    httplib::Response& res) {
        const std::string roll_no = PathParam(req);

        Principal who;
        if (!AllowStudentRecordRead(guard, students, req, res, roll_no, &who)) {
            return;
        }

        GradeQuery query;
        query.roll_no = roll_no;
        query.teacher_username = TeacherScopeFor(who);
        if (!ReadSemesterParam(req, res, &query.semester)) {
            return;
        }

        const auto items = grades.ForStudent(query);
        if (!items.has_value()) {
            SendError(res, 500, "internal", "could not read grades",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        json list = json::array();
        for (const auto& item : *items) list.push_back(GradeToJson(item));

        // No average or aggregate: F-012 forbids cross-subject averaging, and
        // api.md "Grades aggregate" records that decision.
        res.status = 200;
        res.set_content(json{{"studentRollNo", roll_no}, {"items", list}}.dump(),
                        "application/json");
    });
}

}  // namespace srs
