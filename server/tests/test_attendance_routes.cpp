#include <gtest/gtest.h>

#include <algorithm>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <set>
#include <vector>

#include "attendance/attendance_routes.h"
#include "attendance/attendance_store.h"
#include "record_fakes.h"

// SRS-F-017 / F-018 (AC-017, AC-018 / TC-017, TC-018): correct role-scoped
// records, a server-computed two-decimal percentage, and rejection of a
// present count that exceeds the conducted count.

namespace {

using json = nlohmann::json;

class FakeAttendance final : public srs::AttendanceStore {
public:
    std::optional<std::vector<srs::AttendanceCount>> CountsFor(
        const srs::AttendanceQuery& q) override {
        last_query = q;
        if (fail) return std::nullopt;
        std::vector<srs::AttendanceCount> out;
        for (const auto& row : by_roll[q.roll_no]) {
            if (q.semester != 0 && row.semester != q.semester) continue;
            if (!q.course_code.empty() && row.course_code != q.course_code) continue;
            if (!q.teacher_username.empty()) {
                bool assigned = false;
                for (const auto& code : teaches[q.teacher_username]) {
                    assigned = assigned || code == row.course_code;
                }
                if (!assigned) continue;
            }
            out.push_back(row);
        }
        return out;
    }

    srs::AttendanceWriteResult Write(const srs::AttendanceEntry& entry,
                                     const std::string& teacher, bool update) override {
        if (fail) return srs::AttendanceWriteResult::kError;
        bool assigned = false;
        for (const auto& code : teaches[teacher]) assigned = assigned || code == entry.course_code;
        if (!assigned) return srs::AttendanceWriteResult::kForbidden;
        const std::string key = entry.roll_no + "|" + entry.course_code + "|" + entry.session_date;
        if (update) {
            if (!written.count(key)) return srs::AttendanceWriteResult::kNotFound;
            const bool previous = values[key];
            values[key] = entry.present;
            if (previous != entry.present) {
                for (auto& row : by_roll[entry.roll_no]) {
                    if (row.course_code == entry.course_code) row.present += entry.present ? 1 : -1;
                }
            }
        } else {
            if (!written.insert(key).second) return srs::AttendanceWriteResult::kDuplicate;
            values[key] = entry.present;
            auto& rows = by_roll[entry.roll_no];
            auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
                return row.course_code == entry.course_code;
            });
            if (it == rows.end()) rows.push_back(srs::AttendanceCount{entry.course_code, entry.course_code, 5, 1, entry.present ? 1 : 0});
            else { ++it->conducted; if (entry.present) ++it->present; }
        }
        return srs::AttendanceWriteResult::kOk;
    }

    std::map<std::string, std::vector<srs::AttendanceCount>> by_roll;
    std::map<std::string, std::vector<std::string>> teaches;
    std::optional<srs::AttendanceQuery> last_query;
    bool fail = false;
    std::set<std::string> written;
    std::map<std::string, bool> values;
};

srs::AttendanceCount Row(const std::string& code, int sem, long long conducted, long long present) {
    return srs::AttendanceCount{code, code + " name", sem, conducted, present};
}

class AttendanceRoutes : public srs::test::RecordFixture {
protected:
    void SetUp() override {
        attendance.by_roll["1ABC26CS001"] = {Row("CS501", 5, 40, 34),   // 85.00
                                             Row("CS502", 5, 3, 2),     // 66.67
                                             Row("CS301", 3, 120, 101)};  // 84.17
        attendance.by_roll["1ABC26CS002"] = {Row("CS501", 5, 10, 9)};
        attendance.teaches["teacher"] = {"CS501"};
        RecordFixture::SetUp();
    }

    void configure(httplib::Server& server) override {
        srs::RegisterAttendanceRoutes(server, attendance, students, sessions);
    }

    json Body(const std::string& path, const std::string& token, int expected = 200) {
        const auto res = GetAs(path, token);
        EXPECT_TRUE(res);
        if (!res) return json();
        EXPECT_EQ(res->status, expected) << res->body;
        return json::parse(res->body);
    }

    FakeAttendance attendance;
};


// SRS-24: teacher-only session entry, duplicate protection and explicit updates.
TEST_F(AttendanceRoutes, TeacherRecordsAttendanceForAssignedCourse) {
    const auto res = PostAs("/api/attendance", "teacher-token",
        R"({"rollNo":"1ABC26CS001","courseCode":"CS501","sessionDate":"2026-11-05","status":"present"})");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 201) << res->body;
}

TEST_F(AttendanceRoutes, DuplicateSessionEntryIsConflict) {
    const std::string body = R"({"rollNo":"1ABC26CS001","courseCode":"CS501","sessionDate":"2026-11-05","status":"present"})";
    ASSERT_EQ(PostAs("/api/attendance", "teacher-token", body)->status, 201);
    const auto res = PostAs("/api/attendance", "teacher-token", body);
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 409);
}

TEST_F(AttendanceRoutes, TeacherCanUpdateExistingAttendance) {
    const std::string body = R"({"rollNo":"1ABC26CS001","courseCode":"CS501","sessionDate":"2026-11-05","status":"present"})";
    ASSERT_EQ(PostAs("/api/attendance", "teacher-token", body)->status, 201);
    const auto res = PutAs("/api/attendance/1ABC26CS001/CS501/2026-11-05", "teacher-token",
                           R"({"status":"absent"})");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_FALSE(attendance.values["1ABC26CS001|CS501|2026-11-05"]);
    const auto view = Body("/api/attendance/1ABC26CS001?courseCode=CS501", "admin-token");
    EXPECT_EQ(view["items"][0]["conducted"], 41);
    EXPECT_EQ(view["items"][0]["present"], 34);
    EXPECT_DOUBLE_EQ(view["items"][0]["percentage"].get<double>(), 82.93);
}

TEST_F(AttendanceRoutes, UpdatingMissingSessionReturnsNotFound) {
    const auto res = PutAs("/api/attendance/1ABC26CS001/CS501/2026-11-05", "teacher-token",
                           R"({"status":"absent"})");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

TEST_F(AttendanceRoutes, FutureSessionDateIsRejected) {
    const auto res = PostAs("/api/attendance", "teacher-token",
        R"({"rollNo":"1ABC26CS001","courseCode":"CS501","sessionDate":"2099-11-05","status":"present"})");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

TEST_F(AttendanceRoutes, UnassignedTeacherCannotRecordAttendance) {
    const auto res = PostAs("/api/attendance", "teacher-token",
        R"({"rollNo":"1ABC26CS001","courseCode":"CS502","sessionDate":"2026-11-05","status":"present"})");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
}

TEST_F(AttendanceRoutes, AdminAndStudentCannotRecordAttendance) {
    const std::string body = R"({"rollNo":"1ABC26CS001","courseCode":"CS501","sessionDate":"2026-11-05","status":"present"})";
    EXPECT_EQ(PostAs("/api/attendance", "admin-token", body)->status, 403);
    EXPECT_EQ(PostAs("/api/attendance", "student-token", body)->status, 403);
}

TEST_F(AttendanceRoutes, InvalidAttendanceStatusIsRejected) {
    const auto res = PostAs("/api/attendance", "teacher-token",
        R"({"rollNo":"1ABC26CS001","courseCode":"CS501","sessionDate":"2026-11-05","status":"late"})");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

// --- AC-017: correct role-scoped records ------------------------------------

TEST_F(AttendanceRoutes, StudentReadsOwnAttendance) {
    const auto body = Body("/api/attendance/1ABC26CS001", "student-token");
    EXPECT_EQ(body["studentRollNo"], "1ABC26CS001");
    EXPECT_EQ(body["items"].size(), 3u);
    EXPECT_EQ(attendance.last_query->teacher_username, "");
}

TEST_F(AttendanceRoutes, StudentReadingAnotherStudentIsForbidden) {
    const auto res = GetAs("/api/attendance/1ABC26CS002", "student-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
    EXPECT_FALSE(attendance.last_query.has_value());
}

TEST_F(AttendanceRoutes, TeacherSeesOnlyOwnSubjects) {
    const auto body = Body("/api/attendance/1ABC26CS001", "teacher-token");
    ASSERT_EQ(body["items"].size(), 1u);
    EXPECT_EQ(body["items"][0]["courseCode"], "CS501");
    // overall covers only what the teacher can see, not the hidden subjects.
    EXPECT_EQ(body["overall"]["conducted"], 40);
    EXPECT_EQ(body["overall"]["present"], 34);
    EXPECT_EQ(attendance.last_query->teacher_username, "teacher");
}

TEST_F(AttendanceRoutes, AdminSeesEverything) {
    const auto body = Body("/api/attendance/1ABC26CS001", "admin-token");
    EXPECT_EQ(body["items"].size(), 3u);
}

TEST_F(AttendanceRoutes, FiltersReachTheStore) {
    Body("/api/attendance/1ABC26CS001?semester=5&courseCode=CS502", "admin-token");
    EXPECT_EQ(attendance.last_query->semester, 5);
    EXPECT_EQ(attendance.last_query->course_code, "CS502");
}

// --- AC-018: percentage computed server-side, two decimals -------------------

TEST_F(AttendanceRoutes, PercentagePerCourseIsTwoDecimals) {
    const auto body = Body("/api/attendance/1ABC26CS001", "admin-token");
    std::map<std::string, double> pct;
    for (const auto& item : body["items"]) pct[item["courseCode"]] = item["percentage"];
    EXPECT_DOUBLE_EQ(pct["CS501"], 85.00);
    EXPECT_DOUBLE_EQ(pct["CS502"], 66.67);
    EXPECT_DOUBLE_EQ(pct["CS301"], 84.17);
}

TEST_F(AttendanceRoutes, OverallIsSumOfCountsNotAverageOfPercentages) {
    // 137 / 163 = 84.049… -> 84.05. Averaging the three percentages would give
    // 78.61, which is the classic wrong answer.
    const auto body = Body("/api/attendance/1ABC26CS001", "admin-token");
    EXPECT_EQ(body["overall"]["conducted"], 163);
    EXPECT_EQ(body["overall"]["present"], 137);
    EXPECT_DOUBLE_EQ(body["overall"]["percentage"].get<double>(), 84.05);
}

TEST_F(AttendanceRoutes, NoSessionsMeansNullPercentage) {
    attendance.by_roll["1ABC26CS002"].clear();
    const auto body = Body("/api/attendance/1ABC26CS002", "admin-token");
    EXPECT_TRUE(body["items"].empty());
    EXPECT_EQ(body["overall"]["conducted"], 0);
    EXPECT_TRUE(body["overall"]["percentage"].is_null());
}

TEST_F(AttendanceRoutes, PresentExceedingConductedIsRejected) {
    attendance.by_roll["1ABC26CS001"].push_back(Row("CS999", 5, 4, 5));
    const auto res = GetAs("/api/attendance/1ABC26CS001", "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 500);
    const auto body = json::parse(res->body);
    EXPECT_EQ(body["error"], "internal");
    EXPECT_NE(body["message"].get<std::string>().find("present exceeds conducted"),
              std::string::npos);
}

// --- error paths ------------------------------------------------------------

TEST_F(AttendanceRoutes, NoTokenIs401) {
    const auto res = GetAs("/api/attendance/1ABC26CS001", "");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
}

TEST_F(AttendanceRoutes, UnknownStudentIs404ForStaff) {
    const auto res = GetAs("/api/attendance/NOPE", "teacher-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

TEST_F(AttendanceRoutes, BadSemesterIs400) {
    const auto res = GetAs("/api/attendance/1ABC26CS001?semester=99", "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

TEST_F(AttendanceRoutes, StoreFailureIs500) {
    attendance.fail = true;
    const auto res = GetAs("/api/attendance/1ABC26CS001", "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 500);
}

}  // namespace
