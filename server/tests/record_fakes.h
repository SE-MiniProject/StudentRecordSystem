#pragma once

#include <httplib.h>

#include <map>
#include <optional>
#include <string>

#include "auth/session_store.h"
#include "server_fixture.h"
#include "students/student_store.h"

// Fakes shared by the Sprint 2 record-route tests (grades, attendance,
// timetable). test_student_routes.cpp keeps its own copies in an anonymous
// namespace; these live in a named namespace so the two never collide.

namespace srs::test {

class RecordSessions final : public SessionStore {
public:
    void Add(const std::string& token, const std::string& username, const std::string& role) {
        sessions_[token] = SessionIdentity{username, role};
    }
    std::optional<SessionIdentity> Resolve(const std::string& token) override {
        const auto it = sessions_.find(token);
        if (it == sessions_.end()) return std::nullopt;
        return it->second;
    }
    bool Revoke(const std::string& token) override { return sessions_.erase(token) != 0; }

private:
    std::map<std::string, SessionIdentity> sessions_;
};

// Only Find and OwnerOf matter to the record routes; the rest answer "nothing".
class RecordStudents final : public StudentStore {
public:
    void Add(const std::string& roll_no, const std::string& owner, const std::string& department,
             int semester) {
        Student s;
        s.roll_no = roll_no;
        s.full_name = "Student " + roll_no;
        s.department = department;
        s.semester = semester;
        by_roll[roll_no] = s;
        if (!owner.empty()) owners[roll_no] = owner;
    }

    std::optional<StudentPage> List(const StudentQuery&) override { return StudentPage{}; }
    std::optional<Student> Find(const std::string& roll_no) override {
        const auto it = by_roll.find(roll_no);
        if (it == by_roll.end()) return std::nullopt;
        return it->second;
    }
    std::string OwnerOf(const std::string& roll_no) override {
        const auto it = owners.find(roll_no);
        return it == owners.end() ? std::string() : it->second;
    }
    CreateStudentResult Create(const Student&) override { return CreateStudentResult::kError; }
    UpdateStudentResult Update(const std::string&, const Student&) override {
        return UpdateStudentResult::kError;
    }
    bool Delete(const std::string&) override { return false; }

    std::map<std::string, Student> by_roll;
    std::map<std::string, std::string> owners;
};

// Base fixture: three signed-in roles and two students, the first owned by the
// "student" account. Subclasses register the routes under test in configure().
class RecordFixture : public ServerFixture {
protected:
    void SetUp() override {
        sessions.Add("admin-token", "admin", "admin");
        sessions.Add("teacher-token", "teacher", "teacher");
        sessions.Add("teacher2-token", "teacher2", "teacher");
        sessions.Add("student-token", "student", "student");
        sessions.Add("orphan-token", "orphan", "student");  // a student with no linked roll
        students.Add("1ABC26CS001", "student", "Computer Science", 5);
        students.Add("1ABC26CS002", "", "Computer Science", 5);
        ServerFixture::SetUp();
    }

    httplib::Result GetAs(const std::string& path, const std::string& token) {
        httplib::Headers headers;
        if (!token.empty()) headers.emplace("Authorization", "Bearer " + token);
        return httplib::Client(base_url()).Get(path, headers);
    }

    RecordSessions sessions;
    RecordStudents students;
};

}  // namespace srs::test
