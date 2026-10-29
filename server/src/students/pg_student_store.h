#pragma once

#include <string>

#include "students/student_store.h"

namespace srs {

// The Postgres-backed StudentStore. Holds only the connection string;
// connections are opened per call, matching PgUserStore. When a pool lands,
// only the Acquire step changes in either class.
class PgStudentStore final : public StudentStore {
public:
    explicit PgStudentStore(std::string db_url);

    std::optional<StudentPage> List(const StudentQuery& query) override;
    std::optional<Student> Find(const std::string& roll_no) override;
    std::string OwnerOf(const std::string& roll_no) override;
    CreateStudentResult Create(const Student& student) override;
    UpdateStudentResult Update(const std::string& roll_no, const Student& changes) override;
    bool Delete(const std::string& roll_no) override;

private:
    std::string db_url_;
};

}  // namespace srs