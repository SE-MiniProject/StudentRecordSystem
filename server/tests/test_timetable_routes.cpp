#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "profile/profile_store.h"
#include "record_fakes.h"
#include "timetable/timetable_routes.h"
#include "timetable/timetable_store.h"

// SRS-F-020 / AC-020 / TC-020: Teacher sees their own schedule, Student their
// own class schedule, Admin all; fetching another user's schedule is rejected
// server-side.

namespace {

using json = nlohmann::json;

class FakeTimetable final : public srs::TimetableStore {
public:
    std::optional<std::vector<srs::TimetableSlot>> List(const srs::TimetableQuery& q) override {
        last_query = q;
        if (fail) return std::nullopt;
        std::vector<srs::TimetableSlot> out;
        for (const auto& slot : slots) {
            if (!q.teacher_username.empty() && slot.teacher_username != q.teacher_username) continue;
            if (!q.department.empty() && slot.department != q.department) continue;
            if (q.semester != 0 && slot.semester != q.semester) continue;
            out.push_back(slot);
        }
        return out;
    }

    std::vector<srs::TimetableSlot> slots;
    std::optional<srs::TimetableQuery> last_query;
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

srs::TimetableSlot Slot(long long id, int day, const std::string& start, const std::string& code,
                        const std::string& teacher, const std::string& dept, int sem) {
    srs::TimetableSlot s;
    s.id = id;
    s.day_of_week = day;
    s.start_time = start;
    s.end_time = start.substr(0, 2) == "09" ? "10:00" : "12:00";
    s.course_code = code;
    s.course_name = code + " name";
    s.teacher_username = teacher;
    s.teacher_name = teacher + " full";
    s.department = dept;
    s.semester = sem;
    s.room = "A101";
    return s;
}

class TimetableRoutes : public srs::test::RecordFixture {
protected:
    void SetUp() override {
        // Class "Computer Science / 5" (the demo student's), plus another class.
        timetable.slots = {
            Slot(1, 1, "09:00", "CS501", "teacher", "Computer Science", 5),
            Slot(2, 1, "11:00", "CS502", "teacher2", "Computer Science", 5),
            Slot(3, 2, "09:00", "CS503", "teacher", "Computer Science", 5),
            Slot(4, 3, "11:00", "CS302", "teacher2", "Computer Science", 3),
        };
        profiles.by_name["student"] = srs::Profile{"student", "Demo Student", "student",
                                                   "1ABC26CS001"};
        profiles.by_name["orphan"] = srs::Profile{"orphan", "No Link", "student", ""};
        RecordFixture::SetUp();
    }

    void configure(httplib::Server& server) override {
        srs::RegisterTimetableRoutes(server, timetable, profiles, students, sessions);
    }

    std::vector<long long> Ids(const std::string& path, const std::string& token) {
        const auto res = GetAs(path, token);
        EXPECT_TRUE(res);
        std::vector<long long> ids;
        if (!res) return ids;
        EXPECT_EQ(res->status, 200) << res->body;
        if (res->status != 200) return ids;
        // Parsed into a named value first: ranging over json::parse(...)["items"]
        // would iterate a member of a temporary that has already been destroyed.
        const json body = json::parse(res->body);
        for (const auto& item : body["items"]) ids.push_back(item["id"]);
        return ids;
    }

    int Status(const std::string& path, const std::string& token) {
        const auto res = GetAs(path, token);
        return res ? res->status : -1;
    }

    FakeTimetable timetable;
    FakeProfiles profiles;
};

// --- AC-020: each role sees the right schedule ------------------------------

TEST_F(TimetableRoutes, TeacherSeesOwnSchedule) {
    EXPECT_EQ(Ids("/api/timetable", "teacher-token"), (std::vector<long long>{1, 3}));
    EXPECT_EQ(timetable.last_query->teacher_username, "teacher");
}

TEST_F(TimetableRoutes, OtherTeacherSeesTheirOwn) {
    EXPECT_EQ(Ids("/api/timetable", "teacher2-token"), (std::vector<long long>{2, 4}));
}

TEST_F(TimetableRoutes, StudentSeesOwnClassSchedule) {
    EXPECT_EQ(Ids("/api/timetable", "student-token"), (std::vector<long long>{1, 2, 3}));
    EXPECT_EQ(timetable.last_query->department, "Computer Science");
    EXPECT_EQ(timetable.last_query->semester, 5);
    EXPECT_EQ(timetable.last_query->teacher_username, "");
}

TEST_F(TimetableRoutes, AdminSeesAll) {
    EXPECT_EQ(Ids("/api/timetable", "admin-token").size(), 4u);
}

TEST_F(TimetableRoutes, AdminMayFilterByTeacherOrClass) {
    EXPECT_EQ(Ids("/api/timetable?teacher=teacher2", "admin-token"),
              (std::vector<long long>{2, 4}));
    EXPECT_EQ(Ids("/api/timetable?department=Computer%20Science&semester=3", "admin-token"),
              (std::vector<long long>{4}));
}

TEST_F(TimetableRoutes, SlotCarriesContractFields) {
    const auto res = GetAs("/api/timetable", "teacher-token");
    ASSERT_TRUE(res);
    const auto body = json::parse(res->body);
    EXPECT_EQ(body["scope"]["role"], "teacher");
    EXPECT_EQ(body["scope"]["teacher"], "teacher");
    const auto& slot = body["items"][0];
    EXPECT_EQ(slot["dayOfWeek"], 1);
    EXPECT_EQ(slot["startTime"], "09:00");
    EXPECT_EQ(slot["endTime"], "10:00");
    EXPECT_EQ(slot["courseCode"], "CS501");
    EXPECT_EQ(slot["teacherName"], "teacher full");
    EXPECT_EQ(slot["room"], "A101");
}

// --- AC-020: cross-user fetches are rejected server-side ---------------------

TEST_F(TimetableRoutes, TeacherAskingForAnotherTeacherIs403) {
    EXPECT_EQ(Status("/api/timetable?teacher=teacher2", "teacher-token"), 403);
    EXPECT_FALSE(timetable.last_query.has_value());
}

TEST_F(TimetableRoutes, TeacherNamingThemselvesIsFine) {
    EXPECT_EQ(Ids("/api/timetable?teacher=teacher", "teacher-token"),
              (std::vector<long long>{1, 3}));
}

TEST_F(TimetableRoutes, StudentAskingForATeacherIs403) {
    EXPECT_EQ(Status("/api/timetable?teacher=teacher", "student-token"), 403);
}

TEST_F(TimetableRoutes, StudentAskingForAnotherClassIs403) {
    EXPECT_EQ(Status("/api/timetable?semester=3", "student-token"), 403);
    EXPECT_EQ(Status("/api/timetable?department=ISE", "student-token"), 403);
    EXPECT_FALSE(timetable.last_query.has_value());
}

TEST_F(TimetableRoutes, StudentRepeatingOwnClassIsFine) {
    EXPECT_EQ(Ids("/api/timetable?semester=5", "student-token").size(), 3u);
}

TEST_F(TimetableRoutes, StudentWithoutLinkedRecordIs403) {
    EXPECT_EQ(Status("/api/timetable", "orphan-token"), 403);
}

TEST_F(TimetableRoutes, ByRollNoStudentOwnIsAllowed) {
    EXPECT_EQ(Ids("/api/timetable/1ABC26CS001", "student-token"),
              (std::vector<long long>{1, 2, 3}));
}

TEST_F(TimetableRoutes, ByRollNoStudentOtherIs403) {
    EXPECT_EQ(Status("/api/timetable/1ABC26CS002", "student-token"), 403);
    EXPECT_EQ(Status("/api/timetable/UNKNOWN", "student-token"), 403);
}

TEST_F(TimetableRoutes, ByRollNoTeacherIs403) {
    EXPECT_EQ(Status("/api/timetable/1ABC26CS001", "teacher-token"), 403);
}

TEST_F(TimetableRoutes, ByRollNoAdminWorksAnd404sUnknown) {
    EXPECT_EQ(Ids("/api/timetable/1ABC26CS002", "admin-token").size(), 3u);
    EXPECT_EQ(Status("/api/timetable/UNKNOWN", "admin-token"), 404);
}

// --- error paths ------------------------------------------------------------

TEST_F(TimetableRoutes, NoTokenIs401) {
    EXPECT_EQ(Status("/api/timetable", ""), 401);
    EXPECT_EQ(Status("/api/timetable/1ABC26CS001", ""), 401);
}

TEST_F(TimetableRoutes, BadSemesterIs400) {
    EXPECT_EQ(Status("/api/timetable?semester=x", "admin-token"), 400);
}

TEST_F(TimetableRoutes, StoreFailureIs500) {
    timetable.fail = true;
    EXPECT_EQ(Status("/api/timetable", "admin-token"), 500);
}

}  // namespace
