#include <pqxx/pqxx>

#include <cstdlib>
#include <string>
#include <vector>

#include "grades/grade_store.h"

namespace srs {
namespace {

// The scoping query (DB lane, SRS-F-014).
//
//   $1 roll number
//   $2 semester, 0 = any
//   $3 teacher username, '' = no subject filter
//
// The teacher filter is an EXISTS against teacher_subjects rather than a JOIN,
// so a subject taught by two teachers does not duplicate the grade row. The
// subject is LEFT JOINed so a grade in a code with no subjects row still shows
// to Admin and the Student; it cannot match a teacher, which is correct — no
// teacher is assigned to it.
constexpr const char* kGradesSql =
    "SELECT g.subject_code, "
    "       COALESCE(s.name, g.subject_code)              AS course_name, "
    "       g.score::float8                               AS score, "
    "       COALESCE(s.semester, 0)                       AS semester, "
    "       to_char(g.updated_at AT TIME ZONE 'UTC', "
    "               'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')     AS updated_at "
    "FROM grades g "
    "LEFT JOIN subjects s ON s.code = g.subject_code "
    "WHERE g.roll_no = $1 "
    "  AND ($2 = 0 OR s.semester = $2) "
    "  AND ($3 = '' OR EXISTS ("
    "        SELECT 1 FROM teacher_subjects ts "
    "        WHERE ts.subject_code = g.subject_code AND ts.username = $3)) "
    "ORDER BY COALESCE(s.semester, 0), g.subject_code";

// One statement checks both prerequisites and performs the upsert. This avoids
// trusting a teacher name from the request and prevents an unassigned teacher
// from writing a grade through a direct API call.
constexpr const char* kUpsertGradeSql =
    "WITH eligibility AS ("
    "  SELECT EXISTS (SELECT 1 FROM students WHERE roll_no = $1) AS student_exists, "
    "         EXISTS (SELECT 1 FROM teacher_subjects "
    "                 WHERE username = $2 AND subject_code = $3) AS teacher_assigned"
    "), written AS ("
    "  INSERT INTO grades (roll_no, subject_code, score, updated_at) "
    "  SELECT $1, $3, $4, now() FROM eligibility "
    "  WHERE student_exists AND teacher_assigned "
    "  ON CONFLICT (roll_no, subject_code) DO UPDATE "
    "    SET score = EXCLUDED.score, updated_at = now() "
    "  RETURNING (xmax = 0) AS created"
    ") "
    "SELECT eligibility.student_exists, eligibility.teacher_assigned, written.created "
    "FROM eligibility LEFT JOIN written ON TRUE";

class PgGradeStore final : public GradeStore {
public:
    explicit PgGradeStore(std::string db_url) : db_url_(std::move(db_url)) {}

    std::optional<std::vector<GradeItem>> ForStudent(const GradeQuery& query) override {
        try {
            pqxx::connection conn(db_url_);
            pqxx::read_transaction tx(conn);
            const auto rows = tx.exec(
                kGradesSql, pqxx::params{query.roll_no, query.semester, query.teacher_username});
            std::vector<GradeItem> items;
            items.reserve(rows.size());
            for (const auto& row : rows) {
                GradeItem item;
                item.course_code = row["subject_code"].as<std::string>();
                item.course_name = row["course_name"].as<std::string>();
                item.grade = row["score"].as<double>();
                item.semester = row["semester"].as<int>();
                item.updated_at = row["updated_at"].as<std::string>();
                items.push_back(std::move(item));
            }
            return items;
        } catch (const pqxx::failure&) {
            return std::nullopt;
        }
    }

    GradeWriteResult Upsert(const GradeWrite& grade) override {
        try {
            pqxx::connection conn(db_url_);
            pqxx::work tx(conn);
            const auto rows = tx.exec(
                kUpsertGradeSql,
                pqxx::params{grade.roll_no, grade.teacher_username, grade.subject_code, grade.grade});
            if (rows.empty()) return GradeWriteResult::kError;
            if (!rows[0]["student_exists"].as<bool>()) {
                tx.commit();
                return GradeWriteResult::kStudentNotFound;
            }
            if (!rows[0]["teacher_assigned"].as<bool>()) {
                tx.commit();
                return GradeWriteResult::kTeacherNotAssigned;
            }
            const bool created = rows[0]["created"].as<bool>();
            tx.commit();
            return created ? GradeWriteResult::kCreated : GradeWriteResult::kUpdated;
        } catch (const pqxx::failure&) {
            return GradeWriteResult::kError;
        }
    }

private:
    std::string db_url_;
};

class UnbackedGradeStore final : public GradeStore {
public:
    std::optional<std::vector<GradeItem>> ForStudent(const GradeQuery&) override {
        return std::nullopt;
    }
    GradeWriteResult Upsert(const GradeWrite&) override { return GradeWriteResult::kError; }
};

}  // namespace

GradeStore& DefaultGradeStore() {
    static GradeStore& store = []() -> GradeStore& {
        const char* url = std::getenv("DATABASE_URL");
        if (url != nullptr && *url != '\0') {
            static PgGradeStore pg(url);
            return pg;
        }
        static UnbackedGradeStore unbacked;
        return unbacked;
    }();
    return store;
}

}  // namespace srs
