#include "validation/validation.h"

#include <cctype>
#include <optional>
#include <string>

namespace srs::validation {
namespace detail {
namespace {

// Days in `month` of `year`. Leap years are the reason this is not a table: a
// lookup of 29 for February would accept 2025-02-29.
int DaysInMonth(int year, int month) {
    switch (month) {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31;
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        case 2: {
            // Divisible by 4, except centuries that are not divisible by 400.
            const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
            return leap ? 29 : 28;
        }
        default:
            return 0;
    }
}

// Reads a fixed-width decimal run. Digits only: '+' and whitespace are rejected
// here rather than by the caller, so " 2026" cannot pass as a year.
bool ReadDigits(const std::string& value, std::size_t start, std::size_t count, int* out) {
    if (value.size() < start + count) {
        return false;
    }
    int result = 0;
    for (std::size_t i = start; i < start + count; ++i) {
        const char c = value[i];
        if (c < '0' || c > '9') {
            return false;
        }
        result = result * 10 + (c - '0');
    }
    *out = result;
    return true;
}

bool IsLowerHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// Counts UTF-8 code points rather than bytes, so a 120-byte name of accented
// characters is measured the way a person counts it. Continuation bytes
// (10xxxxxx) are not code points.
std::size_t Utf8Length(const std::string& value) {
    std::size_t count = 0;
    for (const char c : value) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

}  // namespace

bool IsSha256Hex(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (const char c : value) {
        if (!IsLowerHex(c)) {
            return false;
        }
    }
    return true;
}

bool IsCalendarDate(const std::string& value) {
    // YYYY-MM-DD: 10 chars, dashes at 4 and 7, everything else digits.
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return false;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    if (!ReadDigits(value, 0, 4, &year) || !ReadDigits(value, 5, 2, &month) ||
        !ReadDigits(value, 8, 2, &day)) {
        return false;
    }
    if (month < 1 || month > 12 || day < 1) {
        return false;
    }
    return day <= DaysInMonth(year, month);
}

}  // namespace detail

namespace {

// `problem_if_absent` is nullopt when the field may be omitted. An empty string
// cannot express that: it would have to mean either "required with no reason" or
// "optional", and create and update need opposite readings of the same input.
// On create every field is required; on update an absent field means "unchanged",
// which is the one interpretation that must not produce a 400.

// 1-120 characters, not blank. The length bound is the contract's; trimming is
// not, because leading and trailing spaces inside a name are the user's to keep.
void CheckFullName(const std::string& full_name,
                   const std::optional<std::string>& problem_if_absent, FieldErrors* errors) {
    if (full_name.empty()) {
        if (problem_if_absent.has_value()) {
            errors->Add("fullName", *problem_if_absent);
        }
        return;
    }
    if (detail::Utf8Length(full_name) > 120) {
        errors->Add("fullName", "must be 1-120 characters");
    }
}

void CheckDepartment(const std::string& department,
                     const std::optional<std::string>& problem_if_absent, FieldErrors* errors) {
    if (department.empty() && problem_if_absent.has_value()) {
        errors->Add("department", *problem_if_absent);
    }
}

void CheckContact(const std::string& contact, const std::optional<std::string>& problem_if_absent,
                  FieldErrors* errors) {
    if (contact.empty() && problem_if_absent.has_value()) {
        errors->Add("contact", *problem_if_absent);
    }
}

// An empty semester means "absent" for updates; anything else must be 1-8.
void CheckSemester(int semester, bool required, FieldErrors* errors) {
    if (semester == StudentFields::kAbsentSemester) {
        if (required) {
            errors->Add("semester", "required");
        }
        return;
    }
    if (semester < 1 || semester > 8) {
        errors->Add("semester", "must be an integer 1-8");
    }
}

}  // namespace

FieldErrors ValidateStudentCreate(const StudentFields& fields) {
    FieldErrors errors;

    // rollNo is the primary key and is immutable after create, so it is the one
    // field with no "leave unchanged" reading. A character outside the contract's
    // set would make the record addressable by a string the client cannot build.
    if (fields.roll_no.empty()) {
        errors.Add("rollNo", "required");
    } else {
        bool shaped = true;
        for (std::size_t i = 0; i < fields.roll_no.size(); ++i) {
            const char c = fields.roll_no[i];
            // The contract constrains rollNo only by presence and immutability,
            // so this is not a format rule: it rejects characters that would
            // break the route path it is addressed by (GET /api/students/{rollNo}),
            // namely spaces, slashes and control characters.
            const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                 (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
            if (!allowed) {
                shaped = false;
                break;
            }
        }
        if (!shaped || fields.roll_no.size() > 32) {
            errors.Add("rollNo", "must be 1-32 letters, digits, '.', '-' or '_'");
        }
    }

    CheckFullName(fields.full_name, std::string{"required"}, &errors);
    CheckDepartment(fields.department, std::string{"required"}, &errors);
    CheckContact(fields.contact, std::string{"required"}, &errors);

    if (fields.date_of_birth.empty()) {
        errors.Add("dateOfBirth", "required");
    } else if (!detail::IsCalendarDate(fields.date_of_birth)) {
        errors.Add("dateOfBirth", "must be a real date in YYYY-MM-DD form");
    }

    CheckSemester(fields.semester, /*required=*/true, &errors);

    return errors;
}

FieldErrors ValidateStudentUpdate(const StudentFields& fields) {
    FieldErrors errors;

    // Only fields the client actually sent are checked: an omitted field means
    // "unchanged", so it must not be rejected for being absent. nullopt is what
    // says that; a string here would be the reason absence is an error.
    CheckFullName(fields.full_name, std::nullopt, &errors);
    CheckDepartment(fields.department, std::nullopt, &errors);
    CheckContact(fields.contact, std::nullopt, &errors);
    CheckSemester(fields.semester, /*required=*/false, &errors);

    return errors;
}

bool IsValidGrade(double grade) {
    // Written as !(in range) so NaN is rejected: every comparison with NaN is
    // false, so "grade >= 0.0 && grade <= 10.0" would let NaN through.
    return grade >= 0.0 && grade <= 10.0;
}

bool IsValidAttendanceDate(const std::string& date, const std::string& today) {
    if (!detail::IsCalendarDate(date)) {
        return false;
    }
    // Both are zero-padded YYYY-MM-DD, so lexicographic order is date order and
    // no parsing is needed. A future date is rejected; today is allowed.
    return date <= today;
}

bool IsValidAttendanceStatus(const std::string& status) {
    return status == "present" || status == "absent";
}

}  // namespace srs::validation
