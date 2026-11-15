#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "auth/session_store.h"
#include "profile/profile_routes.h"
#include "profile/profile_store.h"
#include "server_fixture.h"

namespace {

class FakeSessions final : public srs::SessionStore {
public:
    std::optional<srs::SessionIdentity> Resolve(const std::string& token) override {
        const auto it = sessions.find(token);
        if (it == sessions.end()) return std::nullopt;
        return it->second;
    }

    bool Revoke(const std::string& token) override { return sessions.erase(token) != 0; }

    std::map<std::string, srs::SessionIdentity> sessions;
};

class FakeProfiles final : public srs::ProfileStore {
public:
    std::optional<srs::Profile> Find(const std::string& username) override {
        last_username = username;
        ++calls;
        const auto it = profiles.find(username);
        if (it == profiles.end()) return std::nullopt;
        return it->second;
    }

    std::map<std::string, srs::Profile> profiles;
    std::string last_username;
    int calls = 0;
};

class ProfileRoutesFixture : public srs::test::ServerFixture {
protected:
    void configure(httplib::Server& server) override {
        srs::RegisterProfileRoutes(server, *profiles_, *sessions_);
    }

    void SetUp() override {
        sessions_ = std::make_unique<FakeSessions>();
        profiles_ = std::make_unique<FakeProfiles>();
        sessions_->sessions.emplace("admin-token", srs::SessionIdentity{"admin.user", "admin"});
        sessions_->sessions.emplace("teacher-token", srs::SessionIdentity{"teacher.user", "teacher"});
        sessions_->sessions.emplace("student-token", srs::SessionIdentity{"student.user", "student"});
        profiles_->profiles.emplace("admin.user", srs::Profile{"admin.user", "Admin User", "ignored", ""});
        profiles_->profiles.emplace("teacher.user", srs::Profile{"teacher.user", "Teacher User", "ignored", ""});
        profiles_->profiles.emplace("student.user", srs::Profile{"student.user", "Student User", "ignored", "1PE21CS001"});
        srs::test::ServerFixture::SetUp();
    }

    httplib::Result GetAs(const std::string& path, const std::string& token) {
        httplib::Headers headers;
        if (!token.empty()) headers.emplace("Authorization", "Bearer " + token);
        return httplib::Client(base_url()).Get(path, headers);
    }

    std::unique_ptr<FakeSessions> sessions_;
    std::unique_ptr<FakeProfiles> profiles_;
};

}  // namespace

TEST_F(ProfileRoutesFixture, ReturnsOnlyTheAuthenticatedUsersProfile) {
    struct Case { const char* token; const char* username; const char* name; const char* role; };
    for (const auto& item : {Case{"admin-token", "admin.user", "Admin User", "admin"},
                             Case{"teacher-token", "teacher.user", "Teacher User", "teacher"},
                             Case{"student-token", "student.user", "Student User", "student"}}) {
        const auto res = GetAs("/api/me?username=attacker", item.token);
        ASSERT_TRUE(res);
        ASSERT_EQ(res->status, 200);
        const auto body = nlohmann::json::parse(res->body);
        EXPECT_EQ(body.at("username"), item.username);
        EXPECT_EQ(body.at("fullName"), item.name);
        EXPECT_EQ(body.at("role"), item.role);
        EXPECT_EQ(profiles_->last_username, item.username);
        if (std::string(item.role) == "student") {
            EXPECT_EQ(body.at("rollNo"), "1PE21CS001");
        } else {
            EXPECT_TRUE(body.at("rollNo").is_null());
        }
    }
    EXPECT_EQ(profiles_->calls, 3);
}

TEST_F(ProfileRoutesFixture, MissingOrUnknownBearerTokenDoesNotReadAProfile) {
    const auto missing = GetAs("/api/me", "");
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, 401);

    const auto unknown = GetAs("/api/me", "unknown-token");
    ASSERT_TRUE(unknown);
    EXPECT_EQ(unknown->status, 401);
    EXPECT_EQ(profiles_->calls, 0);
}

TEST_F(ProfileRoutesFixture, UnknownStoredRoleIsForbiddenWithoutProfileLookup) {
    sessions_->sessions.emplace("unknown-role", srs::SessionIdentity{"root", "superuser"});
    const auto res = GetAs("/api/me", "unknown-role");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 403);
    EXPECT_EQ(profiles_->calls, 0);
}

TEST_F(ProfileRoutesFixture, MissingProfileReturnsNotFoundAndEchoesRequestId) {
    profiles_->profiles.erase("admin.user");
    const auto missing = httplib::Client(base_url()).Get(
        "/api/me", httplib::Headers{{"Authorization", "Bearer admin-token"},
                                    {"X-Request-Id", "profile-lookup-42"}});
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, 404);
    const auto body = nlohmann::json::parse(missing->body);
    EXPECT_EQ(body.at("error"), "not_found");
    EXPECT_EQ(body.at("requestId"), "profile-lookup-42");
}

