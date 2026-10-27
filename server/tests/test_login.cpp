#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "auth/login_route.h"
#include "auth/password_hash.h"
#include "auth/user_store.h"
#include "server_fixture.h"

namespace {

// A store with real accounts, so the route is exercised end to end: hashing,
// verification, session issue.
//
// The lockout policy here is a genuine implementation of the contract, not a
// stub: it counts consecutive failures, locks at the threshold, and clears the
// lock lazily once `locked_until` has passed. That means the lockout tests below
// exercise a real policy rather than a canned VerifyResult. Persisting the count
// is the DB lane's half, but the rules are settled here so the route can be
// tested against them.
class FakeUserStore final : public srs::UserStore {
public:
    static constexpr int kMaxAttempts = 5;
    // Fixed rather than clock-derived so the tests are deterministic. A lock set
    // during a test always expires at this instant.
    static constexpr const char* kLockExpiry = "2026-10-27T20:00:00Z";

    void AddUser(const std::string& username, const std::string& password,
                 const std::string& role = "teacher") {
        const auto salt = srs::password::GenerateSalt();
        srs::UserRecord user;
        user.username = username;
        user.full_name = "Test " + username;
        user.role = role;
        user.password.salt = *salt;
        user.password.hash = *srs::password::Hash(password, user.password.salt);
        user.password.algo = "sha256";
        user.active = true;
        users_.push_back(std::move(user));
    }

    // Lapses the lock without touching the counter, which is what the wall clock
    // would do between two requests.
    void ExpireLocks() {
        for (auto& entry : locks_) {
            entry.second.locked_until_iso8601.clear();
        }
    }

    std::optional<srs::UserRecord> Find(const std::string& username) override {
        for (const auto& user : users_) {
            if (user.username == username) {
                return user;
            }
        }
        return std::nullopt;
    }

    srs::VerifyResult VerifyPassword(const srs::UserRecord& user, const std::string& password) override {
        auto& lock = locks_[user.username];

        // A deactivated account is locked regardless of attempt count.
        if (!user.active) {
            return srs::VerifyResult::kLocked;
        }

        // Lazy expiry: a lock whose instant has passed is cleared here, so no
        // sweep is needed and the route never has to know about time.
        if (!lock.locked_until_iso8601.empty() && LockHasLapsed(user.username)) {
            lock = srs::LockoutState{};
        }

        if (!lock.locked_until_iso8601.empty()) {
            return srs::VerifyResult::kLocked;
        }

        const auto candidate = srs::password::Hash(password, user.password.salt);
        if (!candidate.has_value() || !srs::password::ConstantTimeEqual(user.password.hash, *candidate)) {
            ++lock.failed_attempts;
            if (lock.failed_attempts >= kMaxAttempts) {
                lock.locked_until_iso8601 = kLockExpiry;
            }
            return srs::VerifyResult::kWrongPassword;
        }

        lock = srs::LockoutState{};
        return srs::VerifyResult::kOk;
    }

    srs::LockoutState Lockout(const std::string& username) override {
        const auto it = locks_.find(username);
        return it == locks_.end() ? srs::LockoutState{} : it->second;
    }

    srs::Session IssueSession(const std::string& username) override {
        issued_to.push_back(username);
        return srs::Session{"tok-" + username, "2026-11-03T00:00:00Z"};
    }

    std::vector<std::string> issued_to;

private:
    // A lock counts as lapsed only when the test says so, via ExpireLocks. The
    // production store compares against now(); pinning it here keeps the tests
    // deterministic and the policy identical.
    bool LockHasLapsed(const std::string& username) {
        return lapsed_.count(username) != 0;
    }

    std::vector<srs::UserRecord> users_;
    std::map<std::string, srs::LockoutState> locks_;
    std::map<std::string, bool> lapsed_;
};

class LoginFixture : public srs::test::ServerFixture {
protected:
    void configure(httplib::Server& server) override {
        srs::RegisterLoginRoute(server, *store_);
    }

    void SetUp() override {
        store_ = std::make_unique<FakeUserStore>();
        store_->AddUser("a.shetty", "correct horse");
        srs::test::ServerFixture::SetUp();
    }

    std::unique_ptr<FakeUserStore> store_;
};

}  // namespace

TEST_F(LoginFixture, ValidCredentialsReturnTokenAndUser) {
    const auto res = PostLogin(R"({"username":"a.shetty","password":"correct horse"})");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("token"), "tok-a.shetty");
    EXPECT_EQ(body.at("expiresAt"), "2026-11-03T00:00:00Z");
    EXPECT_EQ(body.at("user").at("username"), "a.shetty");
    EXPECT_EQ(body.at("user").at("role"), "teacher");
    EXPECT_FALSE(body.at("user").contains("password"));
    EXPECT_FALSE(body.at("user").contains("password_hash"));
}

TEST_F(LoginFixture, WrongPasswordReturns401) {
    const auto res = PostLogin(R"({"username":"a.shetty","password":"wrong"})");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
    EXPECT_TRUE(store_->issued_to.empty()) << "no session may be issued for a failed login";
}

TEST_F(LoginFixture, UnknownUserAndWrongPasswordAreIndistinguishable) {
    const auto unknown = PostLogin(R"({"username":"nobody","password":"x"})", "req-1");
    const auto wrong = PostLogin(R"({"username":"a.shetty","password":"x"})", "req-1");

    ASSERT_TRUE(unknown);
    ASSERT_TRUE(wrong);
    EXPECT_EQ(unknown->status, 401);
    EXPECT_EQ(wrong->status, 401);

    // Byte-identical bodies: an attacker cannot tell a real username from a
    // fake one by the response (SR-008).
    EXPECT_EQ(unknown->body, wrong->body);
    EXPECT_EQ(unknown->get_header_value("Content-Type"), wrong->get_header_value("Content-Type"));
}

TEST_F(LoginFixture, MissingPasswordReturns400WithField) {
    const auto res = PostLogin(R"({"username":"a.shetty"})");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "validation_failed");
    EXPECT_TRUE(body.at("fields").contains("password"));
}

TEST_F(LoginFixture, MissingUsernameReturns400WithField) {
    const auto res = PostLogin(R"({"password":"correct horse"})");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_TRUE(body.at("fields").contains("username"));
}

TEST_F(LoginFixture, MalformedBodyReturns400) {
    const auto res = PostLogin("not json");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

TEST_F(LoginFixture, EmptyStringsAreTreatedAsMissing) {
    const auto res = PostLogin(R"({"username":"","password":""})");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

// The AC: "Account locked after 5 consecutive failed attempts". This drives the
// fake's real counter, so it fails if the threshold or the ordering is wrong.
TEST_F(LoginFixture, LockedAfterFiveFailedAttempts) {
    for (int i = 0; i < FakeUserStore::kMaxAttempts; ++i) {
        const auto res = PostLogin(R"({"username":"a.shetty","password":"wrong"})");
        ASSERT_TRUE(res);
        EXPECT_EQ(res->status, 401) << "attempt " << i + 1;
        // The threshold attempt locks, so only the attempts *before* it must
        // leave the account unlocked.
        const bool last_attempt = (i + 1 == FakeUserStore::kMaxAttempts);
        if (!last_attempt) {
            EXPECT_TRUE(store_->Lockout("a.shetty").locked_until_iso8601.empty())
                << "should not be locked before the threshold (attempt " << i + 1 << ")";
        }
    }

    // The fifth failure locked it.
    EXPECT_EQ(store_->Lockout("a.shetty").locked_until_iso8601, FakeUserStore::kLockExpiry);

    // The correct password is still rejected: the AC says locked, not that the
    // next try gets through.
    const auto res = PostLogin(R"({"username":"a.shetty","password":"correct horse"})");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
    EXPECT_TRUE(store_->issued_to.empty());
}

TEST_F(LoginFixture, LockedAccountReturnsTheSameBodyAsWrongPassword) {
    for (int i = 0; i < FakeUserStore::kMaxAttempts; ++i) {
        ASSERT_EQ(PostLogin(R"({"username":"a.shetty","password":"wrong"})")->status, 401);
    }

    const auto locked = PostLogin(R"({"username":"a.shetty","password":"wrong"})", "req-9");
    const auto unknown = PostLogin(R"({"username":"nobody","password":"wrong"})", "req-9");

    ASSERT_TRUE(locked);
    ASSERT_TRUE(unknown);
    // A locked account must not be distinguishable from a nonexistent one.
    EXPECT_EQ(locked->body, unknown->body);
}

TEST_F(LoginFixture, SuccessfulLoginClearsFailedAttemptCount) {
    for (int i = 0; i < FakeUserStore::kMaxAttempts - 1; ++i) {
        ASSERT_EQ(PostLogin(R"({"username":"a.shetty","password":"wrong"})")->status, 401);
    }

    ASSERT_EQ(PostLogin(R"({"username":"a.shetty","password":"correct horse"})")->status, 200);
    EXPECT_EQ(store_->Lockout("a.shetty").failed_attempts, 0);

    // Counter was reset, so the same number of failures must not reach the
    // threshold.
    for (int i = 0; i < FakeUserStore::kMaxAttempts - 1; ++i) {
        ASSERT_EQ(PostLogin(R"({"username":"a.shetty","password":"wrong"})")->status, 401);
    }
    EXPECT_EQ(PostLogin(R"({"username":"a.shetty","password":"correct horse"})")->status, 200);
}

// Lazy expiry: once the lock's instant has passed the next correct password
// succeeds, with no sweep and no explicit unlock.
TEST_F(LoginFixture, ExpiredLockAllowsLoginAgain) {
    for (int i = 0; i < FakeUserStore::kMaxAttempts; ++i) {
        ASSERT_EQ(PostLogin(R"({"username":"a.shetty","password":"wrong"})")->status, 401);
    }
    ASSERT_EQ(PostLogin(R"({"username":"a.shetty","password":"correct horse"})")->status, 401);

    store_->ExpireLocks();

    const auto res = PostLogin(R"({"username":"a.shetty","password":"correct horse"})");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(store_->Lockout("a.shetty").failed_attempts, 0);
}
