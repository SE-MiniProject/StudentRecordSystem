#include <pqxx/pqxx>

#include <cstdlib>
#include <string>
#include <vector>

#include "timetable/timetable_store.h"

namespace srs {
namespace {

// The scoped timetable query (DB lane, SRS-F-020).
//
//   $1 teacher username, '' = any
//   $2 department,       '' = any
//   $3 semester,          0 = any
//
// The route decides which of these to set for which caller: a Teacher always
// gets $1 = their own username; a Student always gets $2/$3 = their own class.
constexpr const char* kTimetableSql =
    "SELECT t.id, t.day_of_week, "
    "       to_char(t.start_time, 'HH24:MI') AS start_time, "
    "       to_char(t.end_time,   'HH24:MI') AS end_time, "
    "       t.subject_code, s.name AS course_name, "
    "       t.teacher_username, u.full_name AS teacher_name, "
    "       t.department, t.semester, t.room "
    "FROM timetable t "
    "JOIN subjects s ON s.code = t.subject_code "
    "JOIN users u    ON u.username = t.teacher_username "
    "WHERE ($1 = '' OR t.teacher_username = $1) "
    "  AND ($2 = '' OR t.department = $2) "
    "  AND ($3 = 0  OR t.semester = $3) "
    "ORDER BY t.day_of_week, t.start_time, t.department, t.semester";

class PgTimetableStore final : public TimetableStore {
public:
    explicit PgTimetableStore(std::string db_url) : db_url_(std::move(db_url)) {}

    std::optional<std::vector<TimetableSlot>> List(const TimetableQuery& q) override {
        try {
            pqxx::connection conn(db_url_);
            pqxx::read_transaction tx(conn);
            const auto rows =
                tx.exec(kTimetableSql, pqxx::params{q.teacher_username, q.department, q.semester});
            std::vector<TimetableSlot> slots;
            slots.reserve(rows.size());
            for (const auto& row : rows) {
                TimetableSlot slot;
                slot.id = row["id"].as<long long>();
                slot.day_of_week = row["day_of_week"].as<int>();
                slot.start_time = row["start_time"].as<std::string>();
                slot.end_time = row["end_time"].as<std::string>();
                slot.course_code = row["subject_code"].as<std::string>();
                slot.course_name = row["course_name"].as<std::string>();
                slot.teacher_username = row["teacher_username"].as<std::string>();
                slot.teacher_name = row["teacher_name"].as<std::string>();
                slot.department = row["department"].as<std::string>();
                slot.semester = row["semester"].as<int>();
                slot.room = row["room"].as<std::string>();
                slots.push_back(std::move(slot));
            }
            return slots;
        } catch (const pqxx::failure&) {
            return std::nullopt;
        }
    }

private:
    std::string db_url_;
};

class UnbackedTimetableStore final : public TimetableStore {
public:
    std::optional<std::vector<TimetableSlot>> List(const TimetableQuery&) override {
        return std::nullopt;
    }
};

}  // namespace

TimetableStore& DefaultTimetableStore() {
    static TimetableStore& store = []() -> TimetableStore& {
        const char* url = std::getenv("DATABASE_URL");
        if (url != nullptr && *url != '\0') {
            static PgTimetableStore pg(url);
            return pg;
        }
        static UnbackedTimetableStore unbacked;
        return unbacked;
    }();
    return store;
}

}  // namespace srs
