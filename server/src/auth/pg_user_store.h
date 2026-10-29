#pragma once

#include <string>

#include "auth/user_store.h"

namespace srs {

// The Postgres-backed UserStore: the implementation the UnbackedUserStore
// placeholder was waiting for. It owns the lockout policy (counter, threshold,
// expiry) because that policy is a property of the stored account, not of the
// route, and api.md defines it in terms of columns.
//
// One instance is safe to share across requests: it holds only the connection
// string. Connections are opened per call, which is wasteful but correct and
// keeps the class free of a connection pool that the project does not yet have.
// When a pool lands, only Acquire() changes.
class PgUserStore final : public UserStore {
public:
    // `db_url` is a libpq connection string, e.g.
    // "postgresql://user:pass@host:5432/dbname". It is not validated here: a bad
    // URL must fail at login with the generic 401, not at construction, so the
    // server still starts and /health still answers.
    explicit PgUserStore(std::string db_url);

    std::optional<UserRecord> Find(const std::string& username) override;
    VerifyResult VerifyPassword(const UserRecord& user, const std::string& password) override;
    LockoutState Lockout(const std::string& username) override;
    Session IssueSession(const std::string& username) override;

private:
    std::string db_url_;
};

}  // namespace srs