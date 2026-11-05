#include "attendance/attendance_routes.h"

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

namespace srs {
namespace {

using json = nlohmann::json;

// The percentage as the wire carries it: a number with two decimals, or null
// when nothing was conducted.
json PercentageJson(long long present, long long conducted) {
    const auto pct = attendance::Percentage(present, conducted);
    return pct.has_value() ? json(*pct) : json(nullptr);
}

}  // namespace

void RegisterAttendanceRoutes(httplib::Server& server, AttendanceStore& attendance,
                              StudentStore& students, SessionStore& sessions) {
    const RbacGuard guard(sessions);

    server.Get(R"(/api/attendance/([^/]+))", [&attendance, &students, guard](
                                                 const httplib::Request& req,
                                                 httplib::Response& res) {
        const std::string roll_no = PathParam(req);
        const std::string request_id = req.get_header_value("X-Request-Id");

        Principal who;
        if (!AllowStudentRecordRead(guard, students, req, res, roll_no, &who)) {
            return;
        }

        AttendanceQuery query;
        query.roll_no = roll_no;
        query.teacher_username = TeacherScopeFor(who);
        query.course_code = req.has_param("courseCode") ? req.get_param_value("courseCode")
                                                        : std::string();
        if (!ReadSemesterParam(req, res, &query.semester)) {
            return;
        }

        const auto counts = attendance.CountsFor(query);
        if (!counts.has_value()) {
            SendError(res, 500, "internal", "could not read attendance", request_id);
            return;
        }

        json items = json::array();
        long long total_conducted = 0;
        long long total_present = 0;
        for (const auto& row : *counts) {
            // AC-018: a present count above the conducted count is rejected,
            // not clamped. The counts query cannot produce one, so reaching this
            // means the data or the query is wrong — which is a server fault,
            // reported as such, and never shown to the user as e.g. 120%.
            if (attendance::CheckTally(row.present, row.conducted) !=
                attendance::TallyCheck::kOk) {
                SendError(res, 500, "internal",
                          "attendance counts are inconsistent for " + row.course_code +
                              ": present exceeds conducted",
                          request_id);
                return;
            }
            total_conducted += row.conducted;
            total_present += row.present;
            items.push_back(json{
                {"courseCode", row.course_code},
                {"courseName", row.course_name},
                {"semester", row.semester},
                {"conducted", row.conducted},
                {"present", row.present},
                {"percentage", PercentageJson(row.present, row.conducted)},
            });
        }

        res.status = 200;
        res.set_content(json{
                            {"studentRollNo", roll_no},
                            {"items", items},
                            {"overall",
                             {{"conducted", total_conducted},
                              {"present", total_present},
                              {"percentage", PercentageJson(total_present, total_conducted)}}},
                        }
                            .dump(),
                        "application/json");
    });
}

}  // namespace srs
