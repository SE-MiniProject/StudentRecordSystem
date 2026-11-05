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

}  // namespace

void RegisterGradeRoutes(httplib::Server& server, GradeStore& grades, StudentStore& students,
                         SessionStore& sessions) {
    const RbacGuard guard(sessions);

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
