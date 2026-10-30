#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "accounts/account_routes.h"
#include "accounts/account_store.h"
#include "auth/session_store.h"
#include "server_fixture.h"
#include "students/student_routes.h"
#include "students/student_store.h"

// Route-level tests for SRS-16 (view/search), SRS-17 (modify account), SRS-19
// (create account) and SRS-26 (delete student). The stores are fakes: this file
// asserts the HTTP contract — status codes, envelopes, camelCase field names and
// who is allowed through the RBAC guard — which is where the route's own logic
// lives. The SQL is covered separately by the live-database checks.

namespace {

using json = nlohmann::json;

class FakeSessionStore final : public srs::SessionStore {
public:
    void AddSession(const std::string& token, const std::string& username,
                    const std::string& role) {
        sessions_[token] = srs::SessionIdentity{username, role};
    }

    std::optional<srs::SessionIdentity> Resolve(const std::string& token) override {
        const auto it = sessions_.find(token);
        if (it == sessions_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool Revoke(const std::string& token) override { return sessions_.erase(token) != 0; }

private:
    std::map<std::string, srs::SessionIdentity> sessions_;
};

class FakeStudentStore final : public srs::StudentStore {
public:
    std::optional<srs::StudentPage> List(const srs::StudentQuery& query) override {
        if (fail_list) {
            return std::nullopt;
        }
        srs::StudentPage page;
        page.limit = query.limit;
        page.offset = query.offset;
        std::vector<srs::Student> matched;
        for (const auto& student : students) {
            if (!query.search.empty() &&
                student.full_name.find(query.search) == std::string::npos &&
                student.roll_no.find(query.search) == std::string::npos) {
                continue;
            }
            if (query.semester != 0 && student.semester != query.semester) {
                continue;
            }
            matched.push_back(student);
        }
        // total is the full match count, before paging — that is what lets the
        // client size the view, and it is the one number paging must not change.
        page.total = static_cast<long long>(matched.size());

        // Apply the page window, as a real store would with LIMIT/OFFSET.
        const std::size_t start = static_cast<std::size_t>(std::max(query.offset, 0));
        if (start < matched.size()) {
            const std::size_t count = static_cast<std::size_t>(std::max(query.limit, 0));
            const std::size_t stop = std::min(matched.size(), start + count);
            for (std::size_t i = start; i < stop; ++i) {
                page.items.push_back(matched[i]);
            }
        }
        return page;
    }

    std::optional<srs::Student> Find(const std::string& roll_no) override {
        const auto it = by_roll.find(roll_no);
        if (it == by_roll.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // Empty string means "no account linked", matching the interface.
    std::string OwnerOf(const std::string& roll_no) override {
        const auto it = owners.find(roll_no);
        return it == owners.end() ? std::string() : it->second;
    }

    srs::CreateStudentResult Create(const srs::Student& student) override {
        if (by_roll.count(student.roll_no) != 0) {
            return srs::CreateStudentResult::kDuplicateRollNo;
        }
        by_roll[student.roll_no] = student;
        students.push_back(student);
        return srs::CreateStudentResult::kOk;
    }

    srs::UpdateStudentResult Update(const std::string& roll_no,
                                    const srs::Student& changes) override {
        auto it = by_roll.find(roll_no);
        if (it == by_roll.end()) {
            return srs::UpdateStudentResult::kNotFound;
        }
        // The contract's partial-update semantics: empty / zero means unchanged.
        if (!changes.full_name.empty()) {
            it->second.full_name = changes.full_name;
        }
        if (!changes.date_of_birth.empty()) {
            it->second.date_of_birth = changes.date_of_birth;
        }
        if (!changes.department.empty()) {
            it->second.department = changes.department;
        }
        if (!changes.contact.empty()) {
            it->second.contact = changes.contact;
        }
        if (changes.semester != 0) {
            it->second.semester = changes.semester;
        }
        for (auto& student : students) {
            if (student.roll_no == roll_no) {
                student = it->second;
            }
        }
        return srs::UpdateStudentResult::kOk;
    }

    bool Delete(const std::string& roll_no) override { return by_roll.erase(roll_no) != 0; }

    bool fail_list = false;
    std::vector<srs::Student> students;
    std::map<std::string, srs::Student> by_roll;
    std::map<std::string, std::string> owners;
};

class FakeAccountStore final : public srs::AccountStore {
public:
    std::optional<std::vector<srs::Account>> List() override { return accounts; }

    srs::CreateAccountResult Create(const std::string& username, const std::string&,
                                    const std::string& full_name,
                                    const std::string& role) override {
        if (by_name.count(username) != 0) {
            return srs::CreateAccountResult::kDuplicateUsername;
        }
        srs::Account account{username, full_name, role, true};
        accounts.push_back(account);
        by_name[username] = account;
        return srs::CreateAccountResult::kOk;
    }

    srs::PatchAccountResult Patch(const std::string& username,
                                  const srs::AccountChanges& changes) override {
        auto it = by_name.find(username);
        if (it == by_name.end()) {
            return srs::PatchAccountResult::kNotFound;
        }
        if (!changes.full_name.empty()) {
            it->second.full_name = changes.full_name;
        }
        if (!changes.role.empty()) {
            it->second.role = changes.role;
        }
        if (changes.active.has_value()) {
            it->second.active = *changes.active;
        }
        // Keep the list view in step with the map: the route re-reads via List()
        // after a patch, so a fake that only updated its map would report the
        // pre-patch account back to the client.
        for (auto& account : accounts) {
            if (account.username == username) {
                account = it->second;
            }
        }
        return srs::PatchAccountResult::kOk;
    }

    std::vector<srs::Account> accounts;
    std::map<std::string, srs::Account> by_name;
};

// One fixture that wires the real routes to fakes, so the tests exercise the
// production handler code rather than a reimplementation of it.
class StudentRoutesFixture : public srs::test::ServerFixture {
protected:
    void SetUp() override {
        sessions = std::make_shared<FakeSessionStore>();
        students = std::make_shared<FakeStudentStore>();
        accounts = std::make_shared<FakeAccountStore>();

        sessions->AddSession("admin-token", "admin", "admin");
        sessions->AddSession("teacher-token", "teacher", "teacher");
        sessions->AddSession("student-token", "student", "student");

        srs::Student student;
        student.roll_no = "1PE21CS001";
        student.full_name = "Demo Student";
        student.department = "CSE";
        student.semester = 5;
        student.contact = "9999999999";
        students->by_roll[student.roll_no] = student;
        students->students.push_back(student);
        students->owners[student.roll_no] = "student";

        srs::Student other;
        other.roll_no = "1PE21CS002";
        other.full_name = "Other Person";
        other.department = "ISE";
        other.semester = 3;
        other.contact = "8888888888";
        students->by_roll[other.roll_no] = other;
        students->students.push_back(other);

        srs::test::ServerFixture::SetUp();
    }

    void configure(httplib::Server& server) override {
        srs::RegisterStudentRoutes(server, *students, *sessions);
        srs::RegisterAccountRoutes(server, *accounts, *sessions);
    }

    httplib::Result WithToken(const std::string& method, const std::string& path,
                              const std::string& token,
                              const std::string& body = std::string()) {
        httplib::Headers headers{{"Authorization", "Bearer " + token}};
        httplib::Client client(base_url());
        if (method == "GET") {
            return client.Get(path, headers);
        }
        if (method == "POST") {
            return client.Post(path, headers, body, "application/json");
        }
        if (method == "PUT") {
            return client.Put(path, headers, body, "application/json");
        }
        if (method == "PATCH") {
            return client.Patch(path, headers, body, "application/json");
        }
        return client.Delete(path, headers);
    }

    std::shared_ptr<FakeSessionStore> sessions;
    std::shared_ptr<FakeStudentStore> students;
    std::shared_ptr<FakeAccountStore> accounts;
};

// --- SRS-16: view and search ------------------------------------------------

TEST_F(StudentRoutesFixture, ListIsPaginatedAndCamelCased) {
    const auto res = WithToken("GET", "/api/students?limit=1&offset=0", "admin-token");
    ASSERT_EQ(res->status, 200);
    const auto body = json::parse(res->body);
    EXPECT_EQ(body["limit"], 1);
    EXPECT_EQ(body["offset"], 0);
    EXPECT_EQ(body["total"], 2);
    ASSERT_EQ(body["items"].size(), 1u);
    // camelCase on the wire, per api.md. snake_case here would be a silent
    // contract break that a client would only notice at runtime.
    EXPECT_TRUE(body["items"][0].contains("rollNo"));
    EXPECT_TRUE(body["items"][0].contains("fullName"));
    EXPECT_TRUE(body["items"][0].contains("dateOfBirth"));
}

TEST_F(StudentRoutesFixture, SearchMatchesNameOrRollNo) {
    const auto by_name = WithToken("GET", "/api/students?search=Other", "teacher-token");
    ASSERT_EQ(by_name->status, 200);
    EXPECT_EQ(json::parse(by_name->body)["total"], 1);

    const auto by_roll = WithToken("GET", "/api/students?search=1PE21CS002", "teacher-token");
    ASSERT_EQ(by_roll->status, 200);
    EXPECT_EQ(json::parse(by_roll->body)["total"], 1);
}

TEST_F(StudentRoutesFixture, SemesterFilterNarrowsThePage) {
    const auto res = WithToken("GET", "/api/students?semester=3", "admin-token");
    ASSERT_EQ(res->status, 200);
    const auto body = json::parse(res->body);
    ASSERT_EQ(body["items"].size(), 1u);
    EXPECT_EQ(body["items"][0]["rollNo"], "1PE21CS002");
}

TEST_F(StudentRoutesFixture, LimitAboveTwoHundredIsRejectedNotClamped) {
    // api.md: "limit above 200 returns 400". Clamping would hide a client bug.
    EXPECT_EQ(WithToken("GET", "/api/students?limit=9999", "admin-token")->status, 400);
    EXPECT_EQ(WithToken("GET", "/api/students?limit=0", "admin-token")->status, 400);
}

TEST_F(StudentRoutesFixture, StudentMayReadOwnRecordOnly) {
    EXPECT_EQ(WithToken("GET", "/api/students/1PE21CS001", "student-token")->status, 200);
    EXPECT_EQ(WithToken("GET", "/api/students/1PE21CS002", "student-token")->status, 403);
    // Listing exposes every record, so it is not self-scope.
    EXPECT_EQ(WithToken("GET", "/api/students", "student-token")->status, 403);
}

TEST_F(StudentRoutesFixture, UnknownRollNumberIs404) {
    EXPECT_EQ(WithToken("GET", "/api/students/NOPE", "admin-token")->status, 404);
}

TEST_F(StudentRoutesFixture, StoreFailureIsNotReportedAsAnEmptyPage) {
    // An empty page and a database error must not look the same to the client:
    // the first says "there are none", the second says "ask again later".
    students->fail_list = true;
    EXPECT_EQ(WithToken("GET", "/api/students", "admin-token")->status, 500);
}

// --- SRS-17: modify student ------------------------------------------------

TEST_F(StudentRoutesFixture, UpdateIsAdminOnly) {
    const std::string body = R"({"department":"ECE","semester":6})";
    EXPECT_EQ(WithToken("PUT", "/api/students/1PE21CS002", "admin-token", body)->status, 200);
    EXPECT_EQ(WithToken("PUT", "/api/students/1PE21CS002", "teacher-token", body)->status, 403);
    EXPECT_EQ(WithToken("PUT", "/api/students/1PE21CS002", "student-token", body)->status, 403);
}

TEST_F(StudentRoutesFixture, UpdateOfMissingStudentIs404) {
    const std::string body = R"({"semester":6})";
    EXPECT_EQ(WithToken("PUT", "/api/students/NOPE", "admin-token", body)->status, 404);
}

TEST_F(StudentRoutesFixture, UpdateRejectsInvalidFieldsWithPerFieldEnvelope) {
    const std::string body = R"({"semester":99})";
    const auto res = WithToken("PUT", "/api/students/1PE21CS002", "admin-token", body);
    ASSERT_EQ(res->status, 400);
    const auto envelope = json::parse(res->body);
    EXPECT_EQ(envelope["error"]["code"], "validation_failed");
    EXPECT_TRUE(envelope["error"]["fields"].contains("semester"));
}

// --- SRS-19: create student and account ------------------------------------

TEST_F(StudentRoutesFixture, CreateStudentRequiresEveryField) {
    const auto res = WithToken("POST", "/api/students", "admin-token",
                               R"({"rollNo":"x y","fullName":"","department":"","semester":99})");
    ASSERT_EQ(res->status, 400);
    const auto fields = json::parse(res->body)["error"]["fields"];
    for (const char* field : {"rollNo", "fullName", "department", "semester"}) {
        EXPECT_TRUE(fields.contains(field)) << "missing " << field;
    }
}

TEST_F(StudentRoutesFixture, CreateStudentSucceedsAndIsReadableAfterwards) {
    const auto created = WithToken(
        "POST", "/api/students", "admin-token",
        R"({"rollNo":"1PE22CS009","fullName":"Fresh One","department":"CSE",)"
        R"("semester":3,"dateOfBirth":"2004-05-06","contact":"7777777777"})");
    ASSERT_EQ(created->status, 201);
    EXPECT_EQ(json::parse(created->body)["rollNo"], "1PE22CS009");
    EXPECT_EQ(WithToken("GET", "/api/students/1PE22CS009", "admin-token")->status, 200);
}

TEST_F(StudentRoutesFixture, DuplicateRollNumberIs409NotAnOverwrite) {
    const std::string body =
        R"({"rollNo":"1PE21CS001","fullName":"Clash","department":"CSE",)"
        R"("semester":3,"dateOfBirth":"2004-05-06","contact":"7777777777"})";
    EXPECT_EQ(WithToken("POST", "/api/students", "admin-token", body)->status, 409);
}

TEST_F(StudentRoutesFixture, CreateStudentIsAdminOnly) {
    const std::string body =
        R"({"rollNo":"1PE22CS010","fullName":"X","department":"CSE",)"
        R"("semester":3,"dateOfBirth":"2004-05-06","contact":"7777777777"})";
    EXPECT_EQ(WithToken("POST", "/api/students", "teacher-token", body)->status, 403);
    EXPECT_EQ(WithToken("POST", "/api/students", "student-token", body)->status, 403);
}

TEST_F(StudentRoutesFixture, CreateAccountIsAdminOnlyAndNeverEchoesSecrets) {
    const std::string body =
        R"({"username":"newteach","password":"secret123","fullName":"New Teacher","role":"teacher"})";
    EXPECT_EQ(WithToken("POST", "/api/accounts", "teacher-token", body)->status, 403);
    EXPECT_EQ(WithToken("POST", "/api/accounts", "student-token", body)->status, 403);

    const auto created = WithToken("POST", "/api/accounts", "admin-token", body);
    ASSERT_EQ(created->status, 201);
    const auto account = json::parse(created->body);
    EXPECT_EQ(account["username"], "newteach");
    EXPECT_FALSE(account.contains("password"));
    EXPECT_FALSE(account.contains("passwordHash"));
    EXPECT_FALSE(account.contains("passwordSalt"));
}

TEST_F(StudentRoutesFixture, DuplicateAccountIs409) {
    const std::string body =
        R"({"username":"dup","password":"secret123","fullName":"Dup","role":"teacher"})";
    ASSERT_EQ(WithToken("POST", "/api/accounts", "admin-token", body)->status, 201);
    EXPECT_EQ(WithToken("POST", "/api/accounts", "admin-token", body)->status, 409);
}

TEST_F(StudentRoutesFixture, AccountRoleMustBeOneOfTheThree) {
    const std::string body =
        R"({"username":"weird","password":"secret123","fullName":"W","role":"superuser"})";
    EXPECT_EQ(WithToken("POST", "/api/accounts", "admin-token", body)->status, 400);
}

TEST_F(StudentRoutesFixture, ListAccountsIsAdminOnlyAndHasNoDigests) {
    EXPECT_EQ(WithToken("GET", "/api/accounts", "teacher-token")->status, 403);
    const auto res = WithToken("GET", "/api/accounts", "admin-token");
    ASSERT_EQ(res->status, 200);
    for (const auto& account : json::parse(res->body)["items"]) {
        EXPECT_FALSE(account.contains("passwordHash"));
        EXPECT_FALSE(account.contains("passwordSalt"));
    }
}

TEST_F(StudentRoutesFixture, DeactivatingAnAccountRoundTrips) {
    const auto res =
        WithToken("PATCH", "/api/accounts/newteach", "admin-token", R"({"active":false})");
    // Not created here, so the store reports "no such account".
    EXPECT_EQ(res->status, 404);

    ASSERT_EQ(WithToken("POST", "/api/accounts", "admin-token",
                        R"({"username":"newteach","password":"secret123",)"
                        R"("fullName":"New Teacher","role":"teacher"})")
                  ->status,
              201);
    const auto patched =
        WithToken("PATCH", "/api/accounts/newteach", "admin-token", R"({"active":false})");
    ASSERT_EQ(patched->status, 200);
    EXPECT_EQ(json::parse(patched->body)["active"], false);
}

// --- SRS-26: delete student ------------------------------------------------

TEST_F(StudentRoutesFixture, DeleteIsAdminOnlyAndRemovesTheRecord) {
    EXPECT_EQ(WithToken("DELETE", "/api/students/1PE21CS001", "teacher-token")->status, 403);
    EXPECT_EQ(WithToken("DELETE", "/api/students/1PE21CS001", "admin-token")->status, 204);
    EXPECT_EQ(WithToken("GET", "/api/students/1PE21CS001", "admin-token")->status, 404);
}

TEST_F(StudentRoutesFixture, DeleteOfMissingStudentIs404) {
    EXPECT_EQ(WithToken("DELETE", "/api/students/NOPE", "admin-token")->status, 404);
}

// --- RBAC: the forged-request clause ---------------------------------------

TEST_F(StudentRoutesFixture, ForgedAndAbsentTokensAreRejected) {
    httplib::Client client(base_url());
    EXPECT_EQ(client.Get("/api/students")->status, 401);
    EXPECT_EQ(client.Get("/api/students",
                        httplib::Headers{{"Authorization", "Bearer forged-token"}})
                  ->status,
              401);
    // A role that is not one of the three cannot be invented by writing it into
    // the sessions table either.
    sessions->AddSession("bogus-role", "someone", "superuser");
    EXPECT_EQ(client.Get("/api/students",
                        httplib::Headers{{"Authorization", "Bearer bogus-role"}})
                  ->status,
              403);
}

}  // namespace