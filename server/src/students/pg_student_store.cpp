#include "students/pg_student_store.h"

#include <pqxx/pqxx>

#include <cstdlib>
#include <string>
#include <vector>

namespace srs {
namespace {

// Every query below selects the same columns in the same order, so one decoder
// keeps the spelling of `date_of_birth` and the null handling in one place.
//
// `Row` is a dependent type (pqxx::row or pqxx::row_ref depending on version),
// so `.as<T>()` needs the `template` keyword to be parsed as a template-id
// rather than as a comparison chain.
template <typename Row>
Student Decode(const Row& row) {
    Student student;
    student.roll_no = row["roll_no"].template as<std::string>();
    student.full_name = row["name"].template as<std::string>();
    student.date_of_birth = row["date_of_birth"].is_null()
                                ? std::string()
                                : row["date_of_birth"].template as<std::string>();
    student.department = row["department"].template as<std::string>();
    student.contact = row["contact_details"].template as<std::string>();
    student.semester = row["semester"].template as<int>();
    return student;
}

constexpr const char* kSelectColumns =
    "roll_no, name, date_of_birth::text AS date_of_birth, department, semester, contact_details";

// The store used when DATABASE_URL is unset: every operation reports "nothing
// here", so the routes are reachable and correctly shaped on a server with no
// database, and no request ever appears to succeed against missing storage.
class UnbackedStudentStore final : public StudentStore {
public:
    std::optional<StudentPage> List(const StudentQuery& query) override {
        StudentPage page;
        page.limit = query.limit;
        page.offset = query.offset;
        return page;
    }
    std::optional<Student> Find(const std::string&) override { return std::nullopt; }
    std::string OwnerOf(const std::string&) override { return std::string(); }
    CreateStudentResult Create(const Student&) override { return CreateStudentResult::kError; }
    UpdateStudentResult Update(const std::string&, const Student&) override {
        return UpdateStudentResult::kError;
    }
    bool Delete(const std::string&) override { return false; }
};

}  // namespace

PgStudentStore::PgStudentStore(std::string db_url) : db_url_(std::move(db_url)) {}

std::optional<StudentPage> PgStudentStore::List(const StudentQuery& query) {
    try {
        pqxx::connection conn(db_url_);
        pqxx::read_transaction tx(conn);

        // Filters are appended with the placeholder number they will bind to, and
        // the bind values are collected in the same order. Building the SQL and
        // its parameters together is what keeps a placeholder from being
        // referenced in a branch that does not bind it: an unbound $n is a
        // runtime error, not a compile error, so the two lists must not be
        // maintained separately.
        std::string where = " WHERE 1=1";
        std::vector<std::string> binds;
        if (!query.search.empty()) {
            where += " AND (name ILIKE $" + std::to_string(binds.size() + 1) +
                     " OR roll_no ILIKE $" + std::to_string(binds.size() + 1) + ")";
            binds.push_back("%" + query.search + "%");
        }
        if (query.semester != 0) {
            where += " AND semester = $" + std::to_string(binds.size() + 1);
            binds.push_back(std::to_string(query.semester));
        }

        const std::string count_sql = "SELECT count(*) FROM students" + where;
        const std::string page_sql =
            std::string("SELECT ") + kSelectColumns + " FROM students" + where +
            " ORDER BY roll_no LIMIT $" + std::to_string(binds.size() + 1) + " OFFSET $" +
            std::to_string(binds.size() + 2);

        StudentPage page;
        page.limit = query.limit;
        page.offset = query.offset;

        // pqxx::params has no vector constructor, so the collected values are
        // appended in order. append_multi takes the whole range in one call.
        pqxx::params count_params;
        count_params.append_multi(binds);

        pqxx::params page_params;
        page_params.append_multi(binds);
        page_params.append(query.limit);
        page_params.append(query.offset);

        const auto count = tx.exec(count_sql, count_params);
        page.total = count[0][0].as<long long>();
        const auto rows = tx.exec(page_sql, page_params);
        for (const auto& row : rows) page.items.push_back(Decode(row));
        return page;
    } catch (const pqxx::failure&) {
        return std::nullopt;
    }
}

std::optional<Student> PgStudentStore::Find(const std::string& roll_no) {
    try {
        pqxx::connection conn(db_url_);
        pqxx::read_transaction tx(conn);
        const std::string sql = std::string("SELECT ") + kSelectColumns +
                                " FROM students WHERE roll_no = $1";
        const auto rows = tx.exec(sql, pqxx::params{roll_no});
        if (rows.empty()) return std::nullopt;
        return Decode(rows[0]);
    } catch (const pqxx::failure&) {
        return std::nullopt;
    }
}

std::string PgStudentStore::OwnerOf(const std::string& roll_no) {
    try {
        pqxx::connection conn(db_url_);
        pqxx::read_transaction tx(conn);
        const auto rows =
            tx.exec("SELECT username FROM users WHERE roll_no = $1", pqxx::params{roll_no});
        if (rows.empty()) return std::string();
        return rows[0]["username"].as<std::string>();
    } catch (const pqxx::failure&) {
        // A lookup failure must deny, not allow: the caller treats an empty
        // owner as "nobody owns this", and the role check still applies.
        return std::string();
    }
}

CreateStudentResult PgStudentStore::Create(const Student& student) {
    try {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);

        // Checked inside the same transaction as the insert so a concurrent
        // create of the same roll number cannot slip between check and write;
        // the primary key is the real guarantee, this only produces the 409.
        const auto existing =
            tx.exec("SELECT 1 FROM students WHERE roll_no = $1", pqxx::params{student.roll_no});
        if (!existing.empty()) {
            tx.abort();
            return CreateStudentResult::kDuplicateRollNo;
        }

        tx.exec(
            "INSERT INTO students (roll_no, name, date_of_birth, department, semester, "
            "contact_details) VALUES ($1, $2, $3, $4, $5, $6)",
            pqxx::params{student.roll_no, student.full_name, student.date_of_birth,
                         student.department, student.semester, student.contact});
        tx.commit();
        return CreateStudentResult::kOk;
    } catch (const pqxx::unique_violation&) {
        // Lost the race against another create: still a conflict, not an error.
        return CreateStudentResult::kDuplicateRollNo;
    } catch (const pqxx::failure&) {
        return CreateStudentResult::kError;
    }
}

UpdateStudentResult PgStudentStore::Update(const std::string& roll_no, const Student& changes) {
    try {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);

        // COALESCE per column implements "omitted fields are unchanged" without
        // building the SET list dynamically: an empty string or zero semester
        // means "keep the stored value". NULLIF keeps a deliberately blank
        // string from being treated as "no change".
        const auto rows = tx.exec(
            "UPDATE students SET "
            "  name = COALESCE(NULLIF($2, ''), name), "
            "  department = COALESCE(NULLIF($3, ''), department), "
            "  contact_details = COALESCE(NULLIF($4, ''), contact_details), "
            "  semester = CASE WHEN $5 = 0 THEN semester ELSE $5 END, "
            "  updated_at = now() "
            "WHERE roll_no = $1 RETURNING roll_no",
            pqxx::params{roll_no, changes.full_name, changes.department, changes.contact,
                         changes.semester});
        if (rows.empty()) {
            tx.abort();
            return UpdateStudentResult::kNotFound;
        }
        tx.commit();
        return UpdateStudentResult::kOk;
    } catch (const pqxx::failure&) {
        return UpdateStudentResult::kError;
    }
}

bool PgStudentStore::Delete(const std::string& roll_no) {
    try {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);
        // The students -> grades/attendance foreign keys are ON DELETE CASCADE,
        // so this one statement is the whole operation (F-011).
        const auto rows =
            tx.exec("DELETE FROM students WHERE roll_no = $1 RETURNING roll_no",
                    pqxx::params{roll_no});
        if (rows.empty()) {
            tx.abort();
            return false;
        }
        tx.commit();
        return true;
    } catch (const pqxx::failure&) {
        return false;
    }
}

StudentStore& DefaultStudentStore() {
    static StudentStore& store = []() -> StudentStore& {
        const char* url = std::getenv("DATABASE_URL");
        if (url != nullptr && *url != '\0') {
            static PgStudentStore pg(url);
            return pg;
        }
        static UnbackedStudentStore unbacked;
        return unbacked;
    }();
    return store;
}

}  // namespace srs