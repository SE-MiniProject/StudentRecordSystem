#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "grades/grade_routes.h"
#include "grades/grade_store.h"
#include "profile/profile_routes.h"
#include "profile/profile_store.h"
#include "record_fakes.h"

// SRS-F-014 / AC-014 / TC-014: grades are displayed per role, and a Student's
// attempt to read another student's grades is blocked by the server.

namespace {

using json = nlohmann::json;

// Holds grades per roll number plus a teacher -> subjects map, and applies the
// same filter rules the SQL does, so the tests pin down the route's contract:
// which teacher filter it passes for which caller.
class FakeGrades final : public srs::GradeStore {
public:
    std::optional<std::vector<srs::GradeItem>> ForStudent(const srs::GradeQuery& q) override {
        last_query = q;
        if (fail) return std::nullopt;
        std::vector<srs::GradeItem> out;
        for (const auto& item : by_roll[q.roll_no]) {
            if (q.semester != 0 && item.semester != q.semester) continue;
            if (!q.teacher_username.empty()) {
                const auto& mine = teaches[q.teacher_username];
                bool assigned = false;
                for (const auto& code : mine) assigned = assigned || code == item.course_code;
                if (!assigned) continue;
            }
            out.push_back(item);
        }
        return out;
    }

    srs::GradeWriteResult Upsert(const srs::GradeWrite& write) override {
        last_write = write;
        if (fail) return srs::GradeWriteResult::kError;
        if (known_students.count(write.roll_no) == 0) {
            return srs::GradeWriteResult::kStudentNotFound;
        }
        const auto& mine = teaches[write.teacher_username];
        bool assigned = false;
        for (const auto& code : mine) assigned = assigned || code == write.subject_code;
        if (!assigned) return srs::GradeWriteResult::kTeacherNotAssigned;
        const auto key = std::make_pair(write.roll_no, write.subject_code);
        const bool created = stored.count(key) == 0;
        stored[key] = write.grade;
        auto& rows = by_roll[write.roll_no];
        const auto existing = std::find_if(rows.begin(), rows.end(), [&](const auto& item) {
            return item.course_code == write.subject_code;
        });
        if (existing == rows.end()) {
            rows.push_back(srs::GradeItem{write.subject_code, write.subject_code, write.grade, 0,
                                          "2026-11-06T12:00:00Z"});
        } else {
            existing->grade = write.grade;
            existing->updated_at = "2026-11-06T12:00:00Z";
        }
        return created ? srs::GradeWriteResult::kCreated : srs::GradeWriteResult::kUpdated;
    }

    std::map<std::string, std::vector<srs::GradeItem>> by_roll;
    std::map<std::string, std::vector<std::string>> teaches;
    std::set<std::string> known_students;
    std::map<std::pair<std::string, std::string>, double> stored;
    std::optional<srs::GradeQuery> last_query;
    std::optional<srs::GradeWrite> last_write;
    bool fail = false;
};

class FakeProfiles final : public srs::ProfileStore {
public:
    std::optional<srs::Profile> Find(const std::string& username) override {
        const auto it = by_name.find(username);
        if (it == by_name.end()) return std::nullopt;
        return it->second;
    }
    std::map<std::string, srs::Profile> by_name;
};

srs::GradeItem Grade(const std::string& code, const std::string& name, double grade, int sem) {
    return srs::GradeItem{code, name, grade, sem, "2026-11-01T09:00:00Z"};
}

class GradeRoutes : public srs::test::RecordFixture {
protected:
    void SetUp() override {
        grades.by_roll["1ABC26CS001"] = {Grade("CS501", "DBMS", 8.5, 5),
                                         Grade("CS502", "Operating Systems", 9.0, 5),
                                         Grade("CS301", "Data Structures", 7.0, 3)};
        grades.by_roll["1ABC26CS002"] = {Grade("CS501", "DBMS", 7.5, 5)};
        grades.teaches["teacher"] = {"CS501", "CS503"};
        grades.teaches["teacher2"] = {"CS502"};
        grades.known_students = {"1ABC26CS001", "1ABC26CS002"};
        grades.stored[{"1ABC26CS001", "CS501"}] = 8.5;

        profiles.by_name["student"] = srs::Profile{"student", "Demo Student", "student",
                                                   "1ABC26CS001"};
        profiles.by_name["admin"] = srs::Profile{"admin", "System Administrator", "admin", ""};
        RecordFixture::SetUp();
    }

    void configure(httplib::Server& server) override {
        srs::RegisterGradeRoutes(server, grades, students, sessions);
        srs::RegisterProfileRoutes(server, profiles, sessions);
    }

    httplib::Result PostAs(const std::string& path, const std::string& body,
                           const std::string& token) {
        httplib::Headers headers;
        if (!token.empty()) headers.emplace("Authorization", "Bearer " + token);
        return httplib::Client(base_url()).Post(path, headers, body, "application/json");
    }

    static std::vector<std::string> Codes(const json& body) {
        std::vector<std::string> codes;
        for (const auto& item : body["items"]) codes.push_back(item["courseCode"]);
        return codes;
    }

    FakeGrades grades;
    FakeProfiles profiles;
};

// --- SRS-23: teacher grade entry and update -------------------------------

TEST_F(GradeRoutes, TeacherCreatesGradeForAssignedSubject) {
    const auto res = PostAs("/api/grades",
                            R"({"studentRollNo":"1ABC26CS001","courseCode":"CS501","grade":9.2})",
                            "teacher-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);  // the seeded (student, subject) pair is updated
    const auto body = json::parse(res->body);
    EXPECT_EQ(body["studentRollNo"], "1ABC26CS001");
    EXPECT_EQ(body["courseCode"], "CS501");
    EXPECT_DOUBLE_EQ(body["grade"], 9.2);
    EXPECT_FALSE(body["created"]);
    const auto refreshed = GetAs("/api/grades/1ABC26CS001", "teacher-token");
    ASSERT_TRUE(refreshed);
    EXPECT_DOUBLE_EQ(json::parse(refreshed->body)["items"][0]["grade"], 9.2);
    ASSERT_TRUE(grades.last_write.has_value());
    EXPECT_EQ(grades.last_write->teacher_username, "teacher");
}

TEST_F(GradeRoutes, TeacherCreatesNewGrade) {
    const auto res = PostAs("/api/grades",
                            R"({"studentRollNo":"1ABC26CS001","courseCode":"CS503","grade":7.5})",
                            "teacher-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 201);
    EXPECT_TRUE(json::parse(res->body)["created"]);
    const auto refreshed = GetAs("/api/grades/1ABC26CS001", "teacher-token");
    ASSERT_TRUE(refreshed);
    EXPECT_EQ(Codes(json::parse(refreshed->body)), (std::vector<std::string>{"CS501", "CS503"}));
}

TEST_F(GradeRoutes, AdminCannotEnterGrades) {
    const auto res = PostAs("/api/grades",
                            R"({"studentRollNo":"1ABC26CS001","courseCode":"CS501","grade":8.0})",
                            "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
    EXPECT_FALSE(grades.last_write.has_value());
}

TEST_F(GradeRoutes, StudentCannotEnterGrades) {
    const auto res = PostAs("/api/grades",
                            R"({"studentRollNo":"1ABC26CS001","courseCode":"CS501","grade":8.0})",
                            "student-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
}

TEST_F(GradeRoutes, GradeMustBeWithinInclusiveRange) {
    for (const char* value : {"-0.1", "10.1", "8.55", "null", "\"8.0\""}) {
        const std::string body = std::string(R"({"studentRollNo":"1ABC26CS001","courseCode":"CS501","grade":)") + value + "}";
        const auto res = PostAs("/api/grades", body, "teacher-token");
        ASSERT_TRUE(res);
        EXPECT_EQ(res->status, 400) << value;
        EXPECT_TRUE(json::parse(res->body)["fields"].contains("grade"));
    }
}

TEST_F(GradeRoutes, GradeRangeBoundariesAreAccepted) {
    for (const char* value : {"0.0", "10.0"}) {
        const std::string body = std::string(R"({"studentRollNo":"1ABC26CS001","courseCode":"CS501","grade":)") + value + "}";
        const auto res = PostAs("/api/grades", body, "teacher-token");
        ASSERT_TRUE(res);
        EXPECT_EQ(res->status, 200) << value;
    }
}

TEST_F(GradeRoutes, TeacherCannotEnterGradeForUnassignedSubject) {
    const auto res = PostAs("/api/grades",
                            R"({"studentRollNo":"1ABC26CS001","courseCode":"CS502","grade":8.0})",
                            "teacher-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
}

TEST_F(GradeRoutes, GradeEntryUnknownStudentIs404) {
    const auto res = PostAs("/api/grades",
                            R"({"studentRollNo":"NOPE","courseCode":"CS501","grade":8.0})",
                            "teacher-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

TEST_F(GradeRoutes, GradeEntryRequiresAuthentication) {
    const auto res = PostAs("/api/grades",
                            R"({"studentRollNo":"1ABC26CS001","courseCode":"CS501","grade":8.0})",
                            "");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
}

TEST_F(GradeRoutes, GradeEntryRejectsInvalidJson) {
    const auto res = PostAs("/api/grades", "{", "teacher-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

TEST_F(GradeRoutes, GradeEntryStoreFailureIs500) {
    grades.fail = true;
    const auto res = PostAs("/api/grades",
                            R"({"studentRollNo":"1ABC26CS001","courseCode":"CS501","grade":8.0})",
                            "teacher-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 500);
}

// --- TC-014: correct grades per role ----------------------------------------

TEST_F(GradeRoutes, StudentReadsOwnGrades) {
    const auto res = GetAs("/api/grades/1ABC26CS001", "student-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    const auto body = json::parse(res->body);
    EXPECT_EQ(body["studentRollNo"], "1ABC26CS001");
    EXPECT_EQ(Codes(body), (std::vector<std::string>{"CS501", "CS502", "CS301"}));
    // The owning student is not subject-filtered.
    EXPECT_EQ(grades.last_query->teacher_username, "");
}

TEST_F(GradeRoutes, StudentReadingAnotherStudentIsForbidden) {
    const auto res = GetAs("/api/grades/1ABC26CS002", "student-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
    EXPECT_EQ(json::parse(res->body)["error"], "forbidden");
    // Blocked before any grade was read.
    EXPECT_FALSE(grades.last_query.has_value());
}

TEST_F(GradeRoutes, StudentProbingUnknownRollGets403Not404) {
    // A 404 here would tell a student which roll numbers exist.
    const auto res = GetAs("/api/grades/NOPE", "student-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
}

TEST_F(GradeRoutes, StudentWithNoLinkedRollIsForbidden) {
    const auto res = GetAs("/api/grades/1ABC26CS002", "orphan-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
}

TEST_F(GradeRoutes, TeacherSeesOnlyOwnSubjects) {
    const auto res = GetAs("/api/grades/1ABC26CS001", "teacher-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(Codes(json::parse(res->body)), (std::vector<std::string>{"CS501"}));
    EXPECT_EQ(grades.last_query->teacher_username, "teacher");
}

TEST_F(GradeRoutes, DifferentTeacherSeesDifferentSubjects) {
    const auto res = GetAs("/api/grades/1ABC26CS001", "teacher2-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(Codes(json::parse(res->body)), (std::vector<std::string>{"CS502"}));
}

TEST_F(GradeRoutes, AdminSeesEverything) {
    const auto res = GetAs("/api/grades/1ABC26CS001", "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(Codes(json::parse(res->body)).size(), 3u);
    EXPECT_EQ(grades.last_query->teacher_username, "");
}

TEST_F(GradeRoutes, ItemsCarryContractFieldsAndNoAverage) {
    const auto res = GetAs("/api/grades/1ABC26CS001?semester=5", "admin-token");
    ASSERT_TRUE(res);
    const auto body = json::parse(res->body);
    ASSERT_EQ(body["items"].size(), 2u);
    const auto& first = body["items"][0];
    EXPECT_EQ(first["courseCode"], "CS501");
    EXPECT_EQ(first["courseName"], "DBMS");
    EXPECT_DOUBLE_EQ(first["grade"].get<double>(), 8.5);
    EXPECT_EQ(first["semester"], 5);
    EXPECT_EQ(first["updatedAt"], "2026-11-01T09:00:00Z");
    // F-012: no aggregate in the response.
    EXPECT_FALSE(body.contains("average"));
    EXPECT_FALSE(body.contains("overall"));
}

// --- error paths ------------------------------------------------------------

TEST_F(GradeRoutes, NoTokenIs401) {
    const auto res = GetAs("/api/grades/1ABC26CS001", "");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
}

TEST_F(GradeRoutes, UnknownStudentIs404ForStaff) {
    const auto res = GetAs("/api/grades/NOPE", "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

TEST_F(GradeRoutes, BadSemesterIs400) {
    for (const char* bad : {"0", "13", "abc", "5x"}) {
        const auto res = GetAs(std::string("/api/grades/1ABC26CS001?semester=") + bad,
                               "admin-token");
        ASSERT_TRUE(res);
        EXPECT_EQ(res->status, 400) << bad;
        EXPECT_EQ(json::parse(res->body)["fields"]["semester"], "out of range") << bad;
    }
}

TEST_F(GradeRoutes, StoreFailureIs500) {
    grades.fail = true;
    const auto res = GetAs("/api/grades/1ABC26CS001", "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 500);
}

// --- GET /api/me ------------------------------------------------------------

TEST_F(GradeRoutes, MeGivesStudentTheirRollNumber) {
    const auto res = GetAs("/api/me", "student-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    const auto body = json::parse(res->body);
    EXPECT_EQ(body["username"], "student");
    EXPECT_EQ(body["role"], "student");
    EXPECT_EQ(body["rollNo"], "1ABC26CS001");
}

TEST_F(GradeRoutes, MeGivesStaffNullRollNumber) {
    const auto res = GetAs("/api/me", "admin-token");
    ASSERT_TRUE(res);
    EXPECT_TRUE(json::parse(res->body)["rollNo"].is_null());
}

TEST_F(GradeRoutes, MeWithoutTokenIs401) {
    const auto res = GetAs("/api/me", "");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
}

}  // namespace
