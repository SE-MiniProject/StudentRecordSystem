#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "validation/validation.h"

namespace {

using srs::validation::FieldErrors;
using srs::validation::IsValidAttendanceDate;
using srs::validation::IsValidAttendanceStatus;
using srs::validation::IsValidGrade;
using srs::validation::StudentFields;
using srs::validation::ValidateStudentCreate;
using srs::validation::ValidateStudentUpdate;

// True when `errors` reports exactly `field`. The reason text is not asserted:
// the contract fixes the field names, and pinning the prose would make the tests
// fail on rewording rather than on a behaviour change.
bool ReportsField(const FieldErrors& errors, const std::string& field) {
    for (const auto& error : errors.errors) {
        if (error.field == field) {
            return true;
        }
    }
    return false;
}

StudentFields ValidStudent() {
    StudentFields fields;
    // Obviously synthetic: no fixture may be mistakable for a real student.
    fields.roll_no = "TEST-ROLL-001";
    fields.full_name = "Test Student";
    fields.date_of_birth = "2005-04-11";
    fields.department = "CSE-AIML";
    fields.contact = "student@example.edu";
    fields.semester = 3;
    return fields;
}

// --- student create ------------------------------------------------------

TEST(ValidateStudentCreate, AcceptsACompleteRecord) {
    const auto errors = ValidateStudentCreate(ValidStudent());
    EXPECT_TRUE(errors.ok()) << "unexpected rejections: " << (errors.ok() ? "" : errors.errors[0].field);
}

TEST(ValidateStudentCreate, ReportsEveryMissingRequiredFieldAtOnce) {
    // The client shows all bad fields in one pass, so validation must not stop at
    // the first failure: five separate required-field checks are owed.
    const auto errors = ValidateStudentCreate(StudentFields{});

    EXPECT_TRUE(ReportsField(errors, "rollNo"));
    EXPECT_TRUE(ReportsField(errors, "fullName"));
    EXPECT_TRUE(ReportsField(errors, "dateOfBirth"));
    EXPECT_TRUE(ReportsField(errors, "department"));
    EXPECT_TRUE(ReportsField(errors, "contact"));
    EXPECT_TRUE(ReportsField(errors, "semester"));
}

TEST(ValidateStudentCreate, RejectsRollNumberWithSpacesOrSlash) {
    auto fields = ValidStudent();
    fields.roll_no = "TEST ROLL/001";
    EXPECT_TRUE(ReportsField(ValidateStudentCreate(fields), "rollNo"));
}

TEST(ValidateStudentCreate, RejectsOverlongFullName) {
    auto fields = ValidStudent();
    fields.full_name = std::string(121, 'a');
    EXPECT_TRUE(ReportsField(ValidateStudentCreate(fields), "fullName"));

    fields.full_name = std::string(120, 'a');
    EXPECT_TRUE(ValidateStudentCreate(fields).ok());
}

TEST(ValidateStudentCreate, RejectsSemesterOutsideOneToEight) {
    auto fields = ValidStudent();
    for (const int bad : {0, 9, -1, 100}) {
        fields.semester = bad;
        EXPECT_TRUE(ReportsField(ValidateStudentCreate(fields), "semester")) << "semester " << bad;
    }
    for (const int good : {1, 4, 8}) {
        fields.semester = good;
        EXPECT_TRUE(ValidateStudentCreate(fields).ok()) << "semester " << good;
    }
}

TEST(ValidateStudentCreate, RejectsImpossibleDatesThatMatchTheShape) {
    // These are the cases a regex would wave through, which is the reason the
    // check parses instead of pattern-matching.
    auto fields = ValidStudent();
    for (const char* bad : {"2026-02-30", "2025-02-29", "2026-04-31", "2026-13-01", "2026-00-10",
                            "2026-01-00"}) {
        fields.date_of_birth = bad;
        EXPECT_TRUE(ReportsField(ValidateStudentCreate(fields), "dateOfBirth")) << bad;
    }
}

TEST(ValidateStudentCreate, AcceptsLeapDayAndRejectsWrongShape) {
    auto fields = ValidStudent();
    fields.date_of_birth = "2024-02-29";
    EXPECT_TRUE(ValidateStudentCreate(fields).ok());

    for (const char* bad : {"2004-2-29", "11-04-2005", "2005/04/11", "2005-04-11T00:00:00Z",
                            " 2005-04-11", "abcd-ef-gh"}) {
        fields.date_of_birth = bad;
        EXPECT_TRUE(ReportsField(ValidateStudentCreate(fields), "dateOfBirth")) << bad;
    }
}

// --- student update ------------------------------------------------------

TEST(ValidateStudentUpdate, AcceptsAPartialBody) {
    StudentFields fields;
    fields.semester = 5;
    EXPECT_TRUE(ValidateStudentUpdate(fields).ok());
}

TEST(ValidateStudentUpdate, AcceptsAnEmptyBodyAsAChangeToNothing) {
    // Every field is optional on update, so an empty object is valid; the route
    // treats it as a no-op rather than a 400.
    EXPECT_TRUE(ValidateStudentUpdate(StudentFields{}).ok());
}

TEST(ValidateStudentUpdate, StillRejectsAPresentButInvalidSemester) {
    StudentFields fields;
    fields.semester = 12;
    EXPECT_TRUE(ReportsField(ValidateStudentUpdate(fields), "semester"));
}

TEST(ValidateStudentUpdate, RejectsOverlongFullNameWhenPresent) {
    StudentFields fields;
    fields.full_name = std::string(121, 'a');
    EXPECT_TRUE(ReportsField(ValidateStudentUpdate(fields), "fullName"));
}

// --- grades --------------------------------------------------------------

TEST(IsValidGrade, AcceptsTheInclusiveRange) {
    EXPECT_TRUE(IsValidGrade(0.0));
    EXPECT_TRUE(IsValidGrade(10.0));
    EXPECT_TRUE(IsValidGrade(8.5));
}

TEST(IsValidGrade, RejectsOutOfRangeAndNaN) {
    EXPECT_FALSE(IsValidGrade(-0.1));
    EXPECT_FALSE(IsValidGrade(10.1));
    // NaN compares false against everything, so it needs the negated form of the
    // range check to be caught. This test fails if the check is rewritten the
    // "obvious" way.
    EXPECT_FALSE(IsValidGrade(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(IsValidGrade(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(IsValidGrade(-std::numeric_limits<double>::infinity()));
}

// --- attendance ----------------------------------------------------------

TEST(IsValidAttendanceDate, AcceptsPastAndTodayButNotFuture) {
    const std::string today = "2026-10-28";
    EXPECT_TRUE(IsValidAttendanceDate("2026-10-28", today)) << "today is allowed";
    EXPECT_TRUE(IsValidAttendanceDate("2026-10-27", today));
    EXPECT_TRUE(IsValidAttendanceDate("2020-01-01", today));
    EXPECT_FALSE(IsValidAttendanceDate("2026-10-29", today));
    EXPECT_FALSE(IsValidAttendanceDate("2027-01-01", today));
}

TEST(IsValidAttendanceDate, RejectsImpossibleDates) {
    EXPECT_FALSE(IsValidAttendanceDate("2026-02-30", "2026-10-28"));
    EXPECT_FALSE(IsValidAttendanceDate("2025-02-29", "2026-10-28"));
}

TEST(IsValidAttendanceStatus, AllowsExactlyTwoValues) {
    EXPECT_TRUE(IsValidAttendanceStatus("present"));
    EXPECT_TRUE(IsValidAttendanceStatus("absent"));
    // Case and padding are not tolerated: the column is a check constraint and
    // would reject them, so accepting them here would only move the failure.
    EXPECT_FALSE(IsValidAttendanceStatus("Present"));
    EXPECT_FALSE(IsValidAttendanceStatus(" present"));
    EXPECT_FALSE(IsValidAttendanceStatus("late"));
    EXPECT_FALSE(IsValidAttendanceStatus(""));
}

}  // namespace
