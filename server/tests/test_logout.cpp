#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "auth/logout_route.h"
#include "auth/session_store.h"
#include "server_fixture.h"

namespace {

// A store with real live sessions, so the route is exercised end to end: the
// token is genuinely deleted on logout, and the second call fails because the
// session is gone rather than because a canned response said so.
class FakeSessionStore final : public srs::SessionStore {
public:
    void AddSession(const std::string& token, const std::string& username,
                    const std::string& role = "teacher") {
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

    bool Has(const std::string& token) const {
        return sessions_.count(token) != 0;
    }

private:
    std::map<std::string, srs::SessionIdentity> sessions_;
};

class LogoutFixture : public srs::test::ServerFixture {
protected:
    void configure(httplib::Server& server) override {
        srs::RegisterLogoutRoute(server, *store_);
    }

    void SetUp() override {
        store_ = std::make_unique<FakeSessionStore>();
        store_->AddSession("tok-alice", "a.shetty");
        srs::test::ServerFixture::SetUp();
    }

    httplib::Result PostLogout(const std::string& authorization) {
        httplib::Headers headers;
        if (!authorization.empty()) {
            headers.emplace("Authorization", authorization);
        }
        return httplib::Client(base_url()).Post("/api/auth/logout", headers);
    }

    std::unique_ptr<FakeSessionStore> store_;
};

}  // namespace

TEST_F(LogoutFixture, ValidTokenReturns204AndDeletesTheSession) {
    const auto res = PostLogout("Bearer tok-alice");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 204);
    EXPECT_TRUE(res->body.empty()) << "204 must carry no body";
    EXPECT_FALSE(store_->Has("tok-alice"));
}

TEST_F(LogoutFixture, SecondCallWithTheSameTokenReturns401) {
    const auto first = PostLogout("Bearer tok-alice");
    ASSERT_TRUE(first);
    ASSERT_EQ(first->status, 204);

    const auto second = PostLogout("Bearer tok-alice");

    ASSERT_TRUE(second);
    EXPECT_EQ(second->status, 401);

    // The 401 must use the shared error envelope, like every other failure.
    const auto body = nlohmann::json::parse(second->body);
    EXPECT_EQ(body.at("error"), "unauthenticated");
}

TEST_F(LogoutFixture, UnknownTokenReturns401) {
    const auto res = PostLogout("Bearer not-a-session");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
}

TEST_F(LogoutFixture, MissingAuthorizationHeaderReturns401) {
    const auto res = PostLogout(std::string());

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
    EXPECT_TRUE(store_->Has("tok-alice")) << "an unauthenticated request must not revoke anything";
}

TEST_F(LogoutFixture, NonBearerSchemeReturns401) {
    for (const char* header : {"Basic dXNlcjpwYXNz", "tok-alice", "Bearer", "Bearer "}) {
        const auto res = PostLogout(header);
        ASSERT_TRUE(res) << header;
        EXPECT_EQ(res->status, 401) << "header was: " << header;
    }
    EXPECT_TRUE(store_->Has("tok-alice"));
}

TEST_F(LogoutFixture, SchemeIsMatchedCaseInsensitively) {
    const auto res = PostLogout("bEaReR tok-alice");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 204);
    EXPECT_FALSE(store_->Has("tok-alice"));
}

TEST_F(LogoutFixture, ResponseEchoesRequestIdOnFailure) {
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer unknown");
    headers.emplace("X-Request-Id", "req-42");
    const auto res = httplib::Client(base_url()).Post("/api/auth/logout", headers);

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(nlohmann::json::parse(res->body).at("requestId"), "req-42");
}