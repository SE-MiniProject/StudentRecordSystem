#include <cstdlib>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "auth/session_store.h"
#include "auth/user_store.h"

namespace {

TEST(DefaultStores, FailClosedWhenDatabaseUrlIsUnset) {
    const char* previous = std::getenv("DATABASE_URL");
    const std::optional<std::string> previous_value =
        previous == nullptr ? std::nullopt : std::optional<std::string>(previous);
    ASSERT_EQ(::unsetenv("DATABASE_URL"), 0);

    auto& users = srs::DefaultUserStore();
    EXPECT_FALSE(users.Find("missing-user").has_value());
    EXPECT_EQ(users.VerifyPassword(srs::UserRecord{}, "any-password"),
              srs::VerifyResult::kNoSuchUser);
    const auto lockout = users.Lockout("missing-user");
    EXPECT_EQ(lockout.failed_attempts, 0);
    EXPECT_TRUE(lockout.locked_until_iso8601.empty());
    const auto session = users.IssueSession("missing-user");
    EXPECT_TRUE(session.token.empty());
    EXPECT_TRUE(session.expires_at_iso8601.empty());

    auto& sessions = srs::DefaultSessionStore();
    EXPECT_FALSE(sessions.Resolve("unknown-token").has_value());
    EXPECT_FALSE(sessions.Revoke("unknown-token"));

    if (previous_value) {
        EXPECT_EQ(::setenv("DATABASE_URL", previous_value->c_str(), 1), 0);
    } else {
        EXPECT_EQ(::unsetenv("DATABASE_URL"), 0);
    }
}

}  // namespace
