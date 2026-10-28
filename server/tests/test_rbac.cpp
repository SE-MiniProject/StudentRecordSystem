#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "auth/rbac.h"
#include "auth/session_store.h"
#include "server_fixture.h"

namespace {

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

    bool Revoke(const std::string& token) override {
        return sessions_.erase(token) != 0;
    }

private:
    std::map<std::string, srs::SessionIdentity> sessions_;
};

// Probes the guard over real HTTP. Each path encodes one guard question, so the
// tests exercise the same code the CRUD routes will call rather than a copy.
//
//   /probe/authenticated                — Authenticate
//   /probe/admin                        — RequireRole {admin}
//   /probe/teacher-or-admin             — RequireRole {admin, teacher}
//   /probe/self?owner=<username>        — RequireRoleOrOwnership {admin, teacher}
class RbacFixture : public srs::test::ServerFixture {
protected:
    void configure(httplib::Server& server) override {
        const auto guard = std::make_shared<srs::RbacGuard>(*store_);

        const auto respond = [](const httplib::Request& req, httplib::Response& res,
                                srs::GuardResult result) {
            if (!srs::GuardAllows(result, req, res)) {
                return;
            }
            res.status = 200;
            res.set_content(R"({"ok":true})", "application/json");
        };

        server.Get("/probe/authenticated",
                   [guard, respond](const httplib::Request& req, httplib::Response& res) {
                       srs::Principal who;
                       respond(req, res, guard->Authenticate(req, &who));
                   });

        server.Get("/probe/admin",
                   [guard, respond](const httplib::Request& req, httplib::Response& res) {
                       srs::Principal who;
                       respond(req, res, guard->RequireRole(req, {srs::Role::kAdmin}, &who));
                   });

        server.Get("/probe/teacher-or-admin",
                   [guard, respond](const httplib::Request& req, httplib::Response& res) {
                       srs::Principal who;
                       respond(req, res,
                               guard->RequireRole(req, {srs::Role::kAdmin, srs::Role::kTeacher},
                                                  &who));
                   });

        server.Get("/probe/self",
                   [guard, respond](const httplib::Request& req, httplib::Response& res) {
                       srs::Principal who;
                       const auto owner = req.get_param_value("owner");
                       respond(req, res,
                               guard->RequireRoleOrOwnership(
                                   req, {srs::Role::kAdmin, srs::Role::kTeacher}, owner, &who));
                   });
    }

    void SetUp() override {
        store_ = std::make_unique<FakeSessionStore>();
        store_->AddSession("tok-admin", "a.shetty", "admin");
        store_->AddSession("tok-teacher", "r.rao", "teacher");
        store_->AddSession("tok-student", "s.kumar", "student");
        // A session whose stored role is not one of the three. Nothing should
        // grant access on the strength of a string nobody defined.
        store_->AddSession("tok-bogus", "x.mystery", "superuser");
        srs::test::ServerFixture::SetUp();
    }

    httplib::Result GetAs(const std::string& path, const std::string& token) {
        httplib::Headers headers;
        if (!token.empty()) {
            headers.emplace("Authorization", "Bearer " + token);
        }
        return httplib::Client(base_url()).Get(path, headers);
    }

    std::unique_ptr<FakeSessionStore> store_;
};

}  // namespace

TEST_F(RbacFixture, MissingTokenIsUnauthenticated) {
    const auto res = GetAs("/probe/authenticated", std::string());
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
}

TEST_F(RbacFixture, UnknownTokenIsUnauthenticated) {
    const auto res = GetAs("/probe/authenticated", "tok-nobody");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
}

TEST_F(RbacFixture, AnyRealSessionPassesAuthenticate) {
    for (const char* token : {"tok-admin", "tok-teacher", "tok-student"}) {
        const auto res = GetAs("/probe/authenticated", token);
        ASSERT_TRUE(res) << token;
        EXPECT_EQ(res->status, 200) << token;
    }
}

TEST_F(RbacFixture, AdminOnlyRouteRejectsTeacherAndStudent) {
    EXPECT_EQ(GetAs("/probe/admin", "tok-admin")->status, 200);
    EXPECT_EQ(GetAs("/probe/admin", "tok-teacher")->status, 403);
    EXPECT_EQ(GetAs("/probe/admin", "tok-student")->status, 403);
}

TEST_F(RbacFixture, ForbiddenIsNotConfusedWithUnauthenticated) {
    const auto res = GetAs("/probe/admin", "tok-student");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "forbidden");
}

TEST_F(RbacFixture, MultiRoleRouteAllowsBothRoles) {
    EXPECT_EQ(GetAs("/probe/teacher-or-admin", "tok-admin")->status, 200);
    EXPECT_EQ(GetAs("/probe/teacher-or-admin", "tok-teacher")->status, 200);
    EXPECT_EQ(GetAs("/probe/teacher-or-admin", "tok-student")->status, 403);
}

TEST_F(RbacFixture, ForgedRoleInTheRequestIsIgnored) {
    // The client claims admin in a header and in the query string. Neither is
    // read: the role comes from the session store (SR-004).
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer tok-student");
    headers.emplace("X-Role", "admin");
    headers.emplace("Role", "admin");
    const auto res = httplib::Client(base_url()).Get("/probe/admin?role=admin", headers);

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
}

TEST_F(RbacFixture, UnknownStoredRoleIsForbiddenNotAllowed) {
    // Authenticated, so 403 rather than 401; but never 200.
    const auto res = GetAs("/probe/admin", "tok-bogus");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);

    const auto authenticated_only = GetAs("/probe/authenticated", "tok-bogus");
    ASSERT_TRUE(authenticated_only);
    EXPECT_EQ(authenticated_only->status, 403);
}

TEST_F(RbacFixture, StudentMayReadOwnRecord) {
    // The route allows admin and teacher by role; a student gets in only by
    // owning the record.
    EXPECT_EQ(GetAs("/probe/self?owner=s.kumar", "tok-student")->status, 200);
    EXPECT_EQ(GetAs("/probe/self?owner=a.shetty", "tok-student")->status, 403);
}

TEST_F(RbacFixture, OwnershipDoesNotReplaceAuthentication) {
    // No token, even with the right owner: authentication comes first.
    EXPECT_EQ(GetAs("/probe/self?owner=s.kumar", std::string())->status, 401);
}

TEST_F(RbacFixture, RoleHolderMayReadAnyRecord) {
    EXPECT_EQ(GetAs("/probe/self?owner=s.kumar", "tok-teacher")->status, 200);
    EXPECT_EQ(GetAs("/probe/self?owner=s.kumar", "tok-admin")->status, 200);
}

TEST_F(RbacFixture, GuardFailureEchoesRequestId) {
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer tok-student");
    headers.emplace("X-Request-Id", "req-99");
    const auto res = httplib::Client(base_url()).Get("/probe/admin", headers);

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
    EXPECT_EQ(nlohmann::json::parse(res->body).at("requestId"), "req-99");
}

TEST(RoleParsing, AcceptsExactlyTheThreeContractRoles) {
    EXPECT_EQ(srs::ParseRole("admin"), srs::Role::kAdmin);
    EXPECT_EQ(srs::ParseRole("teacher"), srs::Role::kTeacher);
    EXPECT_EQ(srs::ParseRole("student"), srs::Role::kStudent);

    EXPECT_FALSE(srs::ParseRole("Admin").has_value());
    EXPECT_FALSE(srs::ParseRole("superuser").has_value());
    EXPECT_FALSE(srs::ParseRole("").has_value());
    EXPECT_FALSE(srs::ParseRole("admin ").has_value());
}

TEST(RoleParsing, NamesRoundTrip) {
    for (const auto role : {srs::Role::kAdmin, srs::Role::kTeacher, srs::Role::kStudent}) {
        EXPECT_EQ(srs::ParseRole(srs::RoleName(role)), role);
    }
}