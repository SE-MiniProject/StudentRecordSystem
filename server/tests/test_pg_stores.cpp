#include <gtest/gtest.h>

#include <pqxx/pqxx>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "accounts/pg_account_store.h"
#include "attendance/attendance_store.h"
#include "auth/password_hash.h"
#include "auth/pg_session_store.h"
#include "auth/pg_user_store.h"
#include "grades/grade_store.h"
#include "profile/profile_store.h"
#include "students/pg_student_store.h"
#include "timetable/timetable_store.h"

namespace srs {
namespace {

void SetEnv(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

class PgStoreIntegration : public ::testing::Test {
protected:
    void SetUp() override {
        const char* configured = std::getenv("SRS_TEST_DATABASE_URL");
        if (configured == nullptr || *configured == '\0') {
            GTEST_SKIP() << "set SRS_TEST_DATABASE_URL to a disposable migrated PostgreSQL database";
        }
        db_url_ = configured;
        SetEnv("DATABASE_URL", db_url_);

        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string suffix = std::to_string(ticks);
        admin_ = "covadmin" + suffix;
        teacher_ = "covteacher" + suffix;
        teacher2_ = "covteacher2" + suffix;
        student_user_ = "covstudent" + suffix;
        created_user_ = "created_" + admin_;
        student_roll_ = "COV" + suffix;
        course_ = "COV" + suffix;
        course2_ = "COVB" + suffix;
        room_ = "ROOM" + suffix;

        try {
            pqxx::connection conn(db_url_);
            auto tx = pqxx::work(conn);
            tx.exec("SELECT 1 FROM users LIMIT 0");
            tx.exec("SELECT 1 FROM students LIMIT 0");
            tx.exec("SELECT 1 FROM subjects LIMIT 0");
            tx.exec("SELECT 1 FROM teacher_subjects LIMIT 0");
            tx.exec("SELECT 1 FROM timetable LIMIT 0");
            tx.exec("SELECT 1 FROM sessions LIMIT 0");
            tx.exec("SELECT 1 FROM grades LIMIT 0");
            tx.exec("SELECT 1 FROM attendance LIMIT 0");
            tx.exec("INSERT INTO students (roll_no, name, department, semester, contact_details) "
                    "VALUES ($1, 'Coverage Student', 'Coverage Dept', 5, 'coverage@test.invalid')",
                    pqxx::params{student_roll_});
            InsertUser(tx, admin_, "admin", "Coverage Admin");
            InsertUser(tx, teacher_, "teacher", "Coverage Teacher");
            InsertUser(tx, teacher2_, "teacher", "Coverage Teacher Two");
            InsertUser(tx, student_user_, "student", "Coverage Student");
            tx.exec("UPDATE users SET roll_no = $2 WHERE username = $1",
                    pqxx::params{student_user_, student_roll_});
            tx.exec("INSERT INTO subjects (code, name, semester) VALUES ($1, 'Coverage Course', 5)",
                    pqxx::params{course_});
            tx.exec("INSERT INTO subjects (code, name, semester) VALUES ($1, 'Second Coverage Course', 5)",
                    pqxx::params{course2_});
            tx.exec("INSERT INTO teacher_subjects (username, subject_code) VALUES ($1, $2)",
                    pqxx::params{teacher_, course_});
            tx.exec("INSERT INTO teacher_subjects (username, subject_code) VALUES ($1, $2)",
                    pqxx::params{teacher2_, course2_});
            tx.commit();
            fixture_ready_ = true;
        } catch (const pqxx::failure& error) {
            GTEST_SKIP() << "SRS_TEST_DATABASE_URL must point at the migrated test schema: "
                         << error.what();
        }
    }

    void TearDown() override {
        if (!fixture_ready_) return;
        try {
            pqxx::connection conn(db_url_);
            pqxx::work tx(conn);
            tx.exec("DELETE FROM timetable WHERE subject_code IN ($1, $2)",
                    pqxx::params{course_, course2_});
            tx.exec("DELETE FROM users WHERE username IN ($1, $2, $3, $4, $5)",
                    pqxx::params{admin_, teacher_, teacher2_, student_user_, created_user_});
            tx.exec("DELETE FROM students WHERE roll_no = $1", pqxx::params{student_roll_});
            tx.exec("DELETE FROM subjects WHERE code IN ($1, $2)",
                    pqxx::params{course_, course2_});
            tx.commit();
        } catch (const pqxx::failure&) {
            ADD_FAILURE() << "could not clean coverage fixture rows";
        }
    }

    void InsertUser(pqxx::work& tx, const std::string& username, const std::string& role,
                    const std::string& display_name) {
        constexpr const char* salt = "00112233445566778899aabbccddeeff";
        const auto digest = password::Hash("coverage-password", salt);
        ASSERT_TRUE(digest.has_value());
        tx.exec("INSERT INTO users (username, full_name, role, password_hash, password_salt, "
                "password_algo) VALUES ($1, $2, $3, $4, $5, 'sha256')",
                pqxx::params{username, display_name, role, *digest, salt});
    }

    std::string db_url_;
    std::string admin_;
    std::string teacher_;
    std::string teacher2_;
    std::string student_user_;
    std::string created_user_;
    std::string student_roll_;
    std::string course_;
    std::string course2_;
    std::string room_;
    bool fixture_ready_ = false;
};

TEST(PgStoreFallbacks, MissingDatabaseReturnsEmptyOrErrorResults) {
    SetEnv("DATABASE_URL", "");

    const auto students = DefaultStudentStore().List(StudentQuery{ });
    ASSERT_TRUE(students.has_value());
    EXPECT_TRUE(students->items.empty());
    EXPECT_FALSE(DefaultStudentStore().Find("COV000").has_value());
    EXPECT_TRUE(DefaultStudentStore().OwnerOf("COV000").empty());
    EXPECT_EQ(DefaultStudentStore().Create(Student{}), CreateStudentResult::kError);
    EXPECT_EQ(DefaultStudentStore().Update("COV000", Student{}), UpdateStudentResult::kError);
    EXPECT_FALSE(DefaultStudentStore().Delete("COV000"));

    EXPECT_TRUE(DefaultAccountStore().List()->empty());
    EXPECT_EQ(DefaultAccountStore().Create("coverage_user", "password", "Coverage", "student"),
              CreateAccountResult::kError);
    EXPECT_EQ(DefaultAccountStore().Patch("coverage_user", AccountChanges{}),
              PatchAccountResult::kError);
    EXPECT_FALSE(DefaultProfileStore().Find("coverage_user").has_value());
    EXPECT_FALSE(DefaultGradeStore().ForStudent(GradeQuery{}).has_value());
    EXPECT_EQ(DefaultGradeStore().Upsert(GradeWrite{}), GradeWriteResult::kError);
    EXPECT_FALSE(DefaultAttendanceStore().CountsFor(AttendanceQuery{}).has_value());
    EXPECT_EQ(DefaultAttendanceStore().Write(AttendanceEntry{}, "teacher", false),
              AttendanceWriteResult::kError);
    EXPECT_FALSE(DefaultTimetableStore().List(TimetableQuery{}).has_value());
    TimetableSlot slot;
    EXPECT_EQ(DefaultTimetableStore().Create(slot, &slot), CreateTimetableResult::kError);
}

TEST(PgStoreFailures, DatabaseConnectionErrorsFailClosed) {
    const std::string bad_url = "postgresql://nobody:nobody@127.0.0.1:1/no_database?connect_timeout=1";
    SetEnv("DATABASE_URL", bad_url);

    PgUserStore users(bad_url);
    EXPECT_FALSE(users.Find("nobody").has_value());
    EXPECT_EQ(users.VerifyPassword(UserRecord{}, "password"), VerifyResult::kLocked);
    EXPECT_EQ(users.Lockout("nobody").failed_attempts, 0);
    EXPECT_TRUE(users.IssueSession("nobody").token.empty());
    PgSessionStore sessions(bad_url);
    EXPECT_FALSE(sessions.Resolve("some-token").has_value());
    EXPECT_FALSE(sessions.Revoke("some-token"));

    PgAccountStore accounts(bad_url);
    EXPECT_FALSE(accounts.List().has_value());
    EXPECT_EQ(accounts.Create("coverage_user", "password", "Coverage", "student"),
              CreateAccountResult::kError);
    EXPECT_EQ(accounts.Patch("coverage_user", AccountChanges{}), PatchAccountResult::kError);

    PgStudentStore students(bad_url);
    EXPECT_FALSE(students.List(StudentQuery{}).has_value());
    EXPECT_FALSE(students.Find("COV000").has_value());
    EXPECT_TRUE(students.OwnerOf("COV000").empty());
    EXPECT_EQ(students.Create(Student{}), CreateStudentResult::kError);
    EXPECT_EQ(students.Update("COV000", Student{}), UpdateStudentResult::kError);
    EXPECT_FALSE(students.Delete("COV000"));

    EXPECT_FALSE(DefaultProfileStore().Find("nobody").has_value());
    EXPECT_FALSE(DefaultGradeStore().ForStudent(GradeQuery{}).has_value());
    EXPECT_EQ(DefaultGradeStore().Upsert(GradeWrite{}), GradeWriteResult::kError);
    EXPECT_FALSE(DefaultAttendanceStore().CountsFor(AttendanceQuery{}).has_value());
    EXPECT_EQ(DefaultAttendanceStore().Write(AttendanceEntry{}, "teacher", false),
              AttendanceWriteResult::kError);
    EXPECT_FALSE(DefaultTimetableStore().List(TimetableQuery{}).has_value());
    TimetableSlot slot;
    EXPECT_EQ(DefaultTimetableStore().Create(slot, &slot), CreateTimetableResult::kError);
}

TEST_F(PgStoreIntegration, UserSessionAccountAndProfileStoresUsePostgres) {
    PgUserStore users(db_url_);
    const auto found = users.Find(admin_);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->role, "admin");
    EXPECT_EQ(found->full_name, "Coverage Admin");
    EXPECT_FALSE(users.Find("missing_" + admin_).has_value());

    EXPECT_EQ(users.VerifyPassword(*found, "coverage-password"), VerifyResult::kOk);
    EXPECT_EQ(users.VerifyPassword(*found, "wrong-password"), VerifyResult::kWrongPassword);
    EXPECT_EQ(users.Lockout(admin_).failed_attempts, 1);
    UserRecord malformed_digest = *found;
    malformed_digest.password.salt = "not-a-valid-salt";
    EXPECT_EQ(users.VerifyPassword(malformed_digest, "coverage-password"),
              VerifyResult::kWrongPassword);
    EXPECT_EQ(users.Lockout("unregistered_" + admin_).failed_attempts, 0);

    const Session issued = users.IssueSession(admin_);
    ASSERT_FALSE(issued.token.empty());
    EXPECT_FALSE(issued.expires_at_iso8601.empty());
    PgSessionStore sessions(db_url_);
    const auto resolved = sessions.Resolve(issued.token);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->username, admin_);
    EXPECT_EQ(resolved->role, "admin");
    EXPECT_TRUE(sessions.Revoke(issued.token));
    EXPECT_FALSE(sessions.Resolve(issued.token).has_value());
    EXPECT_FALSE(sessions.Revoke(issued.token));
    EXPECT_FALSE(sessions.Resolve("unknown-token").has_value());
    EXPECT_FALSE(sessions.Revoke("unknown-token"));

    const Session idle = users.IssueSession(admin_);
    ASSERT_FALSE(idle.token.empty());
    {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);
        tx.exec("UPDATE sessions SET last_seen_at = now() - interval '31 minutes' "
                "WHERE token_hash = $1",
                pqxx::params{password::HashToken(idle.token).value()});
        tx.commit();
    }
    EXPECT_FALSE(sessions.Resolve(idle.token).has_value());

    const Session expiring = users.IssueSession(admin_);
    ASSERT_FALSE(expiring.token.empty());
    {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);
        tx.exec("UPDATE sessions SET expires_at = now() - interval '1 second' "
                "WHERE token_hash = $1",
                pqxx::params{password::HashToken(expiring.token).value()});
        tx.commit();
    }
    EXPECT_FALSE(sessions.Resolve(expiring.token).has_value());

    auto lockout_user = users.Find(teacher_);
    ASSERT_TRUE(lockout_user.has_value());
    for (int attempt = 0; attempt < 4; ++attempt) {
        EXPECT_EQ(users.VerifyPassword(*lockout_user, "wrong-password"), VerifyResult::kWrongPassword);
    }
    EXPECT_EQ(users.Lockout(teacher_).failed_attempts, 4);
    EXPECT_EQ(users.VerifyPassword(*lockout_user, "wrong-password"), VerifyResult::kLocked);
    EXPECT_FALSE(users.Lockout(teacher_).locked_until_iso8601.empty());
    EXPECT_EQ(users.VerifyPassword(*lockout_user, "coverage-password"), VerifyResult::kLocked);
    {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);
        tx.exec("UPDATE users SET locked_until = now() - interval '1 second' "
                "WHERE username = $1",
                pqxx::params{teacher_});
        tx.commit();
    }
    EXPECT_EQ(users.VerifyPassword(*lockout_user, "coverage-password"), VerifyResult::kOk);
    EXPECT_EQ(users.Lockout(teacher_).failed_attempts, 0);

    AccountStore& accounts = DefaultAccountStore();
    const auto listed = accounts.List();
    ASSERT_TRUE(listed.has_value());
    EXPECT_TRUE(std::any_of(listed->begin(), listed->end(), [this](const Account& account) {
        return account.username == admin_ && account.active;
    }));
    EXPECT_EQ(accounts.Create(created_user_, "new-password", "Created Account", "teacher"),
              CreateAccountResult::kOk);
    EXPECT_EQ(accounts.Create(created_user_, "new-password", "Duplicate", "teacher"),
              CreateAccountResult::kDuplicateUsername);
    AccountChanges changes;
    changes.full_name = "Updated Account";
    changes.role = "student";
    changes.active = false;
    changes.password = "changed-password";
    EXPECT_EQ(accounts.Patch(created_user_, changes), PatchAccountResult::kOk);
    EXPECT_EQ(accounts.Patch("absent_" + created_user_, changes), PatchAccountResult::kNotFound);
    changes = AccountChanges{};
    changes.active = true;
    EXPECT_EQ(accounts.Patch(created_user_, changes), PatchAccountResult::kOk);

    const auto student_profile = DefaultProfileStore().Find(student_user_);
    ASSERT_TRUE(student_profile.has_value());
    EXPECT_EQ(student_profile->roll_no, student_roll_);
    EXPECT_FALSE(DefaultProfileStore().Find("missing_" + admin_).has_value());

    changes = AccountChanges{};
    changes.active = false;
    EXPECT_EQ(accounts.Patch(admin_, changes), PatchAccountResult::kOk);
    EXPECT_FALSE(sessions.Resolve(issued.token).has_value());
    const auto inactive = users.Find(admin_);
    ASSERT_TRUE(inactive.has_value());
    EXPECT_FALSE(inactive->active);
    EXPECT_EQ(users.VerifyPassword(*inactive, "coverage-password"), VerifyResult::kLocked);
}

TEST_F(PgStoreIntegration, StudentStorePagesSearchesUpdatesAndDeletes) {
    PgStudentStore students(db_url_);
    Student second{student_roll_ + "B", "Second Coverage", "2004-01-20", "Coverage Dept",
                   "second@test.invalid", 5};
    ASSERT_EQ(students.Create(second), CreateStudentResult::kOk);
    EXPECT_EQ(students.Create(second), CreateStudentResult::kDuplicateRollNo);

    StudentQuery query;
    query.search = "Coverage";
    query.semester = 5;
    query.limit = 1;
    query.offset = 0;
    const auto page = students.List(query);
    ASSERT_TRUE(page.has_value());
    EXPECT_GE(page->total, 2);
    ASSERT_EQ(page->items.size(), 1u);
    EXPECT_EQ(page->limit, 1);
    EXPECT_EQ(page->items[0].date_of_birth, "");
    EXPECT_TRUE(students.List(StudentQuery{}).has_value());

    const auto saved = students.Find(student_roll_);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->full_name, "Coverage Student");
    EXPECT_FALSE(students.Find("absent_" + student_roll_).has_value());
    EXPECT_EQ(students.OwnerOf(student_roll_), student_user_);
    EXPECT_EQ(students.OwnerOf(student_roll_ + "B"), "");

    Student changes;
    changes.full_name = "Coverage Student Updated";
    changes.semester = 0;
    EXPECT_EQ(students.Update(student_roll_, changes), UpdateStudentResult::kOk);
    EXPECT_EQ(students.Find(student_roll_)->full_name, "Coverage Student Updated");
    EXPECT_EQ(students.Update("absent_" + student_roll_, changes), UpdateStudentResult::kNotFound);
    changes.semester = 99;
    EXPECT_EQ(students.Update(student_roll_, changes), UpdateStudentResult::kError);
    EXPECT_TRUE(students.Delete(student_roll_ + "B"));
    EXPECT_FALSE(students.Delete(student_roll_ + "B"));
}

TEST_F(PgStoreIntegration, GradeAndAttendanceStoresFilterAndWriteRows) {
    GradeStore& grades = DefaultGradeStore();
    GradeWrite grade{student_roll_, course_, teacher_, 8.5};
    EXPECT_EQ(grades.Upsert(grade), GradeWriteResult::kCreated);
    grade.grade = 9.0;
    EXPECT_EQ(grades.Upsert(grade), GradeWriteResult::kUpdated);
    EXPECT_EQ(grades.Upsert(GradeWrite{"absent_" + student_roll_, course_, teacher_, 5.0}),
              GradeWriteResult::kStudentNotFound);
    EXPECT_EQ(grades.Upsert(GradeWrite{student_roll_, course_, "unassigned_" + teacher_, 5.0}),
              GradeWriteResult::kTeacherNotAssigned);
    const auto all_grades = grades.ForStudent(GradeQuery{student_roll_, 0, ""});
    ASSERT_TRUE(all_grades.has_value());
    ASSERT_EQ(all_grades->size(), 1u);
    EXPECT_EQ(all_grades->front().course_name, "Coverage Course");
    EXPECT_EQ(all_grades->front().grade, 9.0);
    EXPECT_TRUE(grades.ForStudent(GradeQuery{student_roll_, 5, teacher_}).has_value());
    const auto no_grades = grades.ForStudent(GradeQuery{student_roll_, 4, ""});
    ASSERT_TRUE(no_grades.has_value());
    EXPECT_TRUE(no_grades->empty());
    {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);
        tx.exec("INSERT INTO grades (roll_no, subject_code, score) VALUES ($1, 'COVUNKNOWN', 7.5)",
                pqxx::params{student_roll_});
        tx.commit();
    }
    const auto unknown_course = grades.ForStudent(GradeQuery{student_roll_, 0, ""});
    ASSERT_TRUE(unknown_course.has_value());
    EXPECT_EQ(unknown_course->size(), 2u);
    const auto unknown_grade = std::find_if(unknown_course->begin(), unknown_course->end(),
                                            [](const GradeItem& item) {
                                                return item.course_code == "COVUNKNOWN";
                                            });
    ASSERT_NE(unknown_grade, unknown_course->end());
    EXPECT_EQ(unknown_grade->course_name, "COVUNKNOWN");
    EXPECT_EQ(unknown_grade->semester, 0);

    AttendanceStore& attendance = DefaultAttendanceStore();
    const AttendanceEntry entry{student_roll_, course_, "2026-11-01", true};
    EXPECT_EQ(attendance.Write(entry, teacher_, false), AttendanceWriteResult::kOk);
    EXPECT_EQ(attendance.Write(entry, teacher_, false), AttendanceWriteResult::kDuplicate);
    EXPECT_EQ(attendance.Write(entry, "unassigned_" + teacher_, true),
              AttendanceWriteResult::kForbidden);
    EXPECT_EQ(attendance.Write(AttendanceEntry{student_roll_, course_, "2026-11-02", false},
                               teacher_, true),
              AttendanceWriteResult::kNotFound);
    EXPECT_EQ(attendance.Write(AttendanceEntry{student_roll_, course_, "2026-11-01", false},
                               teacher_, true),
              AttendanceWriteResult::kOk);
    EXPECT_EQ(attendance.Write(AttendanceEntry{student_roll_, course_, "bad-date", false},
                               teacher_, false),
              AttendanceWriteResult::kError);
    const auto counts = attendance.CountsFor(AttendanceQuery{student_roll_, 0, "", ""});
    ASSERT_TRUE(counts.has_value());
    ASSERT_EQ(counts->size(), 1u);
    EXPECT_EQ(counts->front().conducted, 1);
    EXPECT_EQ(counts->front().present, 0);
    EXPECT_TRUE(attendance.CountsFor(AttendanceQuery{student_roll_, 5, course_, teacher_})
                    .has_value());
    {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);
        tx.exec("INSERT INTO attendance (roll_no, subject_code, session_date, present) "
                "VALUES ($1, 'COVUNKNOWN', '2026-11-03', true)",
                pqxx::params{student_roll_});
        tx.commit();
    }
    const auto unknown_counts = attendance.CountsFor(AttendanceQuery{student_roll_, 0, "", ""});
    ASSERT_TRUE(unknown_counts.has_value());
    EXPECT_EQ(unknown_counts->size(), 2u);
    const auto unknown_count = std::find_if(unknown_counts->begin(), unknown_counts->end(),
                                            [](const AttendanceCount& item) {
                                                return item.course_code == "COVUNKNOWN";
                                            });
    ASSERT_NE(unknown_count, unknown_counts->end());
    EXPECT_EQ(unknown_count->course_name, "COVUNKNOWN");
    EXPECT_EQ(unknown_count->semester, 0);
}

TEST_F(PgStoreIntegration, TimetableStoreListsCreatesAndRejectsConflicts) {
    TimetableStore& timetable = DefaultTimetableStore();
    TimetableSlot slot;
    slot.day_of_week = 2;
    slot.start_time = "09:00";
    slot.end_time = "10:00";
    slot.course_code = course_;
    slot.teacher_username = teacher_;
    slot.department = "Coverage Dept";
    slot.semester = 5;
    slot.room = room_;

    TimetableSlot created;
    EXPECT_EQ(timetable.Create(slot, &created), CreateTimetableResult::kCreated);
    EXPECT_GT(created.id, 0);
    EXPECT_EQ(created.course_name, "Coverage Course");
    EXPECT_EQ(created.teacher_name, "Coverage Teacher");

    TimetableSlot conflict = slot;
    conflict.start_time = "09:30";
    conflict.end_time = "10:30";
    conflict.room = "OTHER";
    EXPECT_EQ(timetable.Create(conflict, &created), CreateTimetableResult::kConflict);
    conflict.teacher_username = teacher2_;
    conflict.course_code = course2_;
    conflict.start_time = "09:00";
    conflict.end_time = "10:00";
    conflict.room = room_;
    EXPECT_EQ(timetable.Create(conflict, &created), CreateTimetableResult::kConflict);
    conflict.teacher_username = "other_" + teacher_;
    conflict.course_code = course_;
    conflict.room = "ANOTHER";
    EXPECT_EQ(timetable.Create(conflict, &created), CreateTimetableResult::kInvalidAssignment);

    TimetableSlot back_to_back = slot;
    back_to_back.start_time = "10:00";
    back_to_back.end_time = "11:00";
    back_to_back.room = "BACKTOBACK";
    EXPECT_EQ(timetable.Create(back_to_back, &created), CreateTimetableResult::kCreated);

    const auto slots = timetable.List(TimetableQuery{teacher_, "Coverage Dept", 5});
    ASSERT_TRUE(slots.has_value());
    ASSERT_EQ(slots->size(), 2u);
    EXPECT_TRUE(timetable.List(TimetableQuery{}).has_value());
}

}  // namespace
}  // namespace srs
