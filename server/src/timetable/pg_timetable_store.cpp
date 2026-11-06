#include <pqxx/pqxx>

#include <cstdlib>
#include <string>
#include <utility>
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

    CreateTimetableResult Create(const TimetableSlot& slot, TimetableSlot* created) override {
        try {
            pqxx::connection conn(db_url_);
            pqxx::work tx(conn);

            // Serialize slot writers so two concurrent requests cannot both
            // pass the overlap query before either commits its insert.
            tx.exec("SELECT pg_advisory_xact_lock(734921, 27)");

            const auto assignment = tx.exec(
                "SELECT 1 FROM teacher_subjects ts "
                "JOIN users u ON u.username = ts.username "
                "JOIN subjects s ON s.code = ts.subject_code "
                "WHERE ts.username = $1 AND ts.subject_code = $2 AND s.semester = $3 "
                "AND u.role = 'teacher' AND u.active",
                pqxx::params{slot.teacher_username, slot.course_code, slot.semester});
            if (assignment.empty()) {
                tx.abort();
                return CreateTimetableResult::kInvalidAssignment;
            }

            const auto conflict = tx.exec(
                "SELECT 1 FROM timetable "
                "WHERE day_of_week = $1 AND semester = $2 "
                "AND (teacher_username = $3 OR room = $4) "
                "AND start_time < $6::time AND end_time > $5::time "
                "LIMIT 1",
                pqxx::params{slot.day_of_week, slot.semester, slot.teacher_username,
                             slot.room, slot.start_time, slot.end_time});
            if (!conflict.empty()) {
                tx.abort();
                return CreateTimetableResult::kConflict;
            }

            const auto rows = tx.exec(
                "INSERT INTO timetable (subject_code, teacher_username, department, semester, "
                "day_of_week, start_time, end_time, room) "
                "VALUES ($1, $2, $3, $4, $5, $6::time, $7::time, $8) "
                "RETURNING id",
                pqxx::params{slot.course_code, slot.teacher_username, slot.department,
                             slot.semester, slot.day_of_week, slot.start_time, slot.end_time,
                             slot.room});
            const long long id = rows[0]["id"].as<long long>();
            const auto inserted = tx.exec(
                "SELECT t.id, t.day_of_week, to_char(t.start_time, 'HH24:MI') AS start_time, "
                "to_char(t.end_time, 'HH24:MI') AS end_time, t.subject_code, s.name AS course_name, "
                "t.teacher_username, u.full_name AS teacher_name, t.department, t.semester, t.room "
                "FROM timetable t JOIN subjects s ON s.code = t.subject_code "
                "JOIN users u ON u.username = t.teacher_username WHERE t.id = $1",
                pqxx::params{id});
            const auto& row = inserted[0];
            created->id = row["id"].as<long long>();
            created->day_of_week = row["day_of_week"].as<int>();
            created->start_time = row["start_time"].as<std::string>();
            created->end_time = row["end_time"].as<std::string>();
            created->course_code = row["subject_code"].as<std::string>();
            created->course_name = row["course_name"].as<std::string>();
            created->teacher_username = row["teacher_username"].as<std::string>();
            created->teacher_name = row["teacher_name"].as<std::string>();
            created->department = row["department"].as<std::string>();
            created->semester = row["semester"].as<int>();
            created->room = row["room"].as<std::string>();
            tx.commit();
            return CreateTimetableResult::kCreated;
        } catch (const pqxx::sql_error& error) {
            if (error.sqlstate() == "23503") return CreateTimetableResult::kInvalidAssignment;
            if (error.sqlstate() == "23505") return CreateTimetableResult::kConflict;
            return CreateTimetableResult::kError;
        } catch (const pqxx::failure&) {
            return CreateTimetableResult::kError;
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
    CreateTimetableResult Create(const TimetableSlot&, TimetableSlot*) override {
        return CreateTimetableResult::kError;
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
