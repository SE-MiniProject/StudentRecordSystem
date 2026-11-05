#include <pqxx/pqxx>

#include <cstdlib>
#include <string>
#include <vector>

#include "attendance/attendance_store.h"

namespace srs {
namespace {

// The counts query per student (DB lane, SRS-F-017/F-018).
//
//   $1 roll number
//   $2 semester, 0 = any
//   $3 course code, '' = any
//   $4 teacher username, '' = no subject filter
//
// conducted = rows for that course (one row per session date, enforced by
// UNIQUE (roll_no, subject_code, session_date) in 002), present = the rows
// marked present. Because present is a filtered count of the same rows, the
// query itself cannot produce present > conducted; the server still checks.
constexpr const char* kCountsSql =
    "SELECT a.subject_code, "
    "       COALESCE(s.name, a.subject_code)             AS course_name, "
    "       COALESCE(s.semester, 0)                      AS semester, "
    "       count(*)                                     AS conducted, "
    "       count(*) FILTER (WHERE a.present)            AS present "
    "FROM attendance a "
    "LEFT JOIN subjects s ON s.code = a.subject_code "
    "WHERE a.roll_no = $1 "
    "  AND ($2 = 0 OR s.semester = $2) "
    "  AND ($3 = '' OR a.subject_code = $3) "
    "  AND ($4 = '' OR EXISTS ("
    "        SELECT 1 FROM teacher_subjects ts "
    "        WHERE ts.subject_code = a.subject_code AND ts.username = $4)) "
    "GROUP BY a.subject_code, s.name, s.semester "
    "ORDER BY COALESCE(s.semester, 0), a.subject_code";

class PgAttendanceStore final : public AttendanceStore {
public:
    explicit PgAttendanceStore(std::string db_url) : db_url_(std::move(db_url)) {}

    std::optional<std::vector<AttendanceCount>> CountsFor(const AttendanceQuery& q) override {
        try {
            pqxx::connection conn(db_url_);
            pqxx::read_transaction tx(conn);
            const auto rows = tx.exec(
                kCountsSql, pqxx::params{q.roll_no, q.semester, q.course_code, q.teacher_username});
            std::vector<AttendanceCount> items;
            items.reserve(rows.size());
            for (const auto& row : rows) {
                AttendanceCount item;
                item.course_code = row["subject_code"].as<std::string>();
                item.course_name = row["course_name"].as<std::string>();
                item.semester = row["semester"].as<int>();
                item.conducted = row["conducted"].as<long long>();
                item.present = row["present"].as<long long>();
                items.push_back(std::move(item));
            }
            return items;
        } catch (const pqxx::failure&) {
            return std::nullopt;
        }
    }

private:
    std::string db_url_;
};

class UnbackedAttendanceStore final : public AttendanceStore {
public:
    std::optional<std::vector<AttendanceCount>> CountsFor(const AttendanceQuery&) override {
        return std::nullopt;
    }
};

}  // namespace

AttendanceStore& DefaultAttendanceStore() {
    static AttendanceStore& store = []() -> AttendanceStore& {
        const char* url = std::getenv("DATABASE_URL");
        if (url != nullptr && *url != '\0') {
            static PgAttendanceStore pg(url);
            return pg;
        }
        static UnbackedAttendanceStore unbacked;
        return unbacked;
    }();
    return store;
}

}  // namespace srs
