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

// SRS-F-020 / AC-020 / TC-020: role-scoped reads. SRS-F-019/F-021:
// Admin slot creation and server-side teacher/room overlap rejection.

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

    srs::CreateTimetableResult Create(const srs::TimetableSlot& slot,
                                      srs::TimetableSlot* created) override {
        if (fail) return srs::CreateTimetableResult::kError;
        if (invalid_assignment) return srs::CreateTimetableResult::kInvalidAssignment;
        for (const auto& old : slots) {
            const bool same_scope = old.day_of_week == slot.day_of_week && old.semester == slot.semester;
            const bool overlaps = old.start_time < slot.end_time && old.end_time > slot.start_time;
            if (same_scope && overlaps &&
                (old.teacher_username == slot.teacher_username || old.room == slot.room))
                return srs::CreateTimetableResult::kConflict;
        }
        *created = slot;
        created->id = next_id++;
        created->course_name = slot.course_code + " name";
        created->teacher_name = slot.teacher_username + " full";
        slots.push_back(*created);
        return srs::CreateTimetableResult::kCreated;
    }

    std::vector<srs::TimetableSlot> slots;
    std::optional<srs::TimetableQuery> last_query;
    bool fail = false;
    bool invalid_assignment = false;
    long long next_id = 10;
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

    httplib::Result CreateAs(const std::string& body, const std::string& token) {
        httplib::Headers headers;
        if (!token.empty()) headers.emplace("Authorization", "Bearer " + token);
        return httplib::Client(base_url()).Post("/api/timetable", headers, body,
                                                "application/json");
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

// --- SRS-F-019 / F-021: create and reject overlapping slots ------------------

const std::string kValidSlot =
    R"({"dayOfWeek":1,"startTime":"13:00","endTime":"14:00","courseCode":"CS501","teacher":"teacher","department":"Computer Science","semester":5,"room":"C101"})";

TEST_F(TimetableRoutes, AdminCreatesAndStoresAValidSlot) {
    const auto res = CreateAs(kValidSlot, "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 201) << res->body;
    const auto body = json::parse(res->body);
    EXPECT_EQ(body["dayOfWeek"], 1);
    EXPECT_EQ(body["startTime"], "13:00");
    EXPECT_EQ(body["endTime"], "14:00");
    EXPECT_EQ(body["courseCode"], "CS501");
    EXPECT_EQ(body["teacher"], "teacher");
    EXPECT_EQ(body["room"], "C101");
    EXPECT_EQ(timetable.slots.size(), 5u);
}

TEST_F(TimetableRoutes, OnlyAdminMayCreateASlot) {
    EXPECT_EQ(CreateAs(kValidSlot, "teacher-token")->status, 403);
    EXPECT_EQ(CreateAs(kValidSlot, "student-token")->status, 403);
    EXPECT_EQ(timetable.slots.size(), 4u);
}

TEST_F(TimetableRoutes, InvalidSlotFieldsAreRejectedBeforeStorage) {
    const auto res = CreateAs(
        R"({"dayOfWeek":8,"startTime":"25:00","endTime":"09:60","courseCode":"","teacher":"t","department":"","semester":0,"room":""})",
        "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    const auto body = json::parse(res->body);
    EXPECT_EQ(body["error"]["code"], "validation_failed");
    EXPECT_TRUE(body["error"]["fields"].contains("dayOfWeek"));
    EXPECT_TRUE(body["error"]["fields"].contains("startTime"));
    EXPECT_TRUE(body["error"]["fields"].contains("endTime"));
    EXPECT_EQ(timetable.slots.size(), 4u);
}

TEST_F(TimetableRoutes, TeacherOverlapOnSameDayAndSemesterIsConflict) {
    const auto res = CreateAs(
        R"({"dayOfWeek":1,"startTime":"09:30","endTime":"10:30","courseCode":"CS503","teacher":"teacher","department":"Computer Science","semester":5,"room":"C101"})",
        "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 409);
    EXPECT_EQ(json::parse(res->body)["error"], "timetable_conflict");
    EXPECT_EQ(timetable.slots.size(), 4u);
}

TEST_F(TimetableRoutes, RoomOverlapWithAnotherTeacherIsConflict) {
    const auto res = CreateAs(
        R"({"dayOfWeek":1,"startTime":"09:30","endTime":"10:30","courseCode":"CS502","teacher":"teacher2","department":"Computer Science","semester":5,"room":"A101"})",
        "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 409);
    EXPECT_EQ(timetable.slots.size(), 4u);
}

TEST_F(TimetableRoutes, TouchingIntervalsAndOtherSemestersAreAllowed) {
    const auto touching = CreateAs(
        R"({"dayOfWeek":1,"startTime":"10:00","endTime":"10:30","courseCode":"CS503","teacher":"teacher","department":"Computer Science","semester":5,"room":"C101"})",
        "admin-token");
    ASSERT_TRUE(touching);
    EXPECT_EQ(touching->status, 201) << touching->body;
    const auto other_semester = CreateAs(
        R"({"dayOfWeek":1,"startTime":"09:30","endTime":"10:30","courseCode":"CS503","teacher":"teacher","department":"Computer Science","semester":3,"room":"A101"})",
        "admin-token");
    ASSERT_TRUE(other_semester);
    EXPECT_EQ(other_semester->status, 201) << other_semester->body;
}

TEST_F(TimetableRoutes, UnassignedTeacherCoursePairIsRejected) {
    timetable.invalid_assignment = true;
    const auto res = CreateAs(kValidSlot, "admin-token");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body)["error"], "invalid_assignment");
    EXPECT_EQ(timetable.slots.size(), 4u);
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
