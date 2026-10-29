#include "auth/pg_user_store.h"

#include <pqxx/pqxx>

#include <optional>
#include <string>
#include <utility>

#include "auth/password_hash.h"

namespace srs {
namespace {

// The policy, in one place. api.md "Account lockout" fixes all three: a wrong
// password increments, the fifth failure locks, the lock lasts 15 minutes.
constexpr int kMaxAttempts = 5;
constexpr const char* kLockInterval = "15 minutes";

}  // namespace

PgUserStore::PgUserStore(std::string db_url) : db_url_(std::move(db_url)) {}

std::optional<UserRecord> PgUserStore::Find(const std::string& username) {
    try {
        pqxx::connection conn(db_url_);
        pqxx::read_transaction tx(conn);
        const auto rows = tx.exec(
            "SELECT username, full_name, role, password_hash, password_salt, password_algo, active "
            "FROM users WHERE username = $1",
            pqxx::params{username});
        if (rows.empty()) {
            return std::nullopt;
        }
        const auto& row = rows[0];
        UserRecord user;
        user.username = row["username"].as<std::string>();
        user.full_name = row["full_name"].as<std::string>();
        user.role = row["role"].as<std::string>();
        user.password.hash = row["password_hash"].as<std::string>();
        user.password.salt = row["password_salt"].as<std::string>();
        user.password.algo = row["password_algo"].as<std::string>();
        user.active = row["active"].as<bool>();
        return user;
    } catch (const pqxx::failure&) {
        // No database, no user. The route renders this as the generic 401, so a
        // broken DB is indistinguishable from a wrong password to the client,
        // which is what SR-008 asks for.
        return std::nullopt;
    }
}

VerifyResult PgUserStore::VerifyPassword(const UserRecord& user, const std::string& password) {
    // A deactivated account is refused before any password work, per api.md:
    // `active = false` is separate from lockout and is not cleared by time.
    if (!user.active) {
        return VerifyResult::kLocked;
    }

    try {
        pqxx::connection conn(db_url_);

        // Lazy expiry, in the same transaction as the decision. A lock whose
        // instant has passed is cleared, so no scheduled job is needed and no
        // request can disagree about when a lock ends.
        {
            pqxx::work tx(conn);
            tx.exec("UPDATE users SET failed_attempts = 0, locked_until = NULL "
                    "WHERE username = $1 AND locked_until IS NOT NULL AND locked_until <= now()",
                    pqxx::params{user.username});
            tx.commit();
        }

        // Read the lock state after expiry has been applied. A lock still in the
        // future refuses the attempt *before* the password is checked, so a
        // correct password does not get through and does not reset the counter.
        {
            pqxx::read_transaction tx(conn);
            const auto rows = tx.exec(
                "SELECT locked_until FROM users WHERE username = $1 AND locked_until > now()",
                pqxx::params{user.username});
            if (!rows.empty()) {
                return VerifyResult::kLocked;
            }
        }

        // Verify. A digest the server cannot compute (bad hex salt, broken
        // CSPRNG) compares unequal, which lands on the same failed-attempt path
        // as a wrong password rather than becoming an oracle.
        const auto computed = password::Hash(password, user.password.salt);
        const bool matches =
            computed.has_value() && password::ConstantTimeEqual(*computed, user.password.hash);

        pqxx::work tx(conn);
        if (matches) {
            // A successful login resets the counter and clears any lapsed lock.
            tx.exec("UPDATE users SET failed_attempts = 0, locked_until = NULL, updated_at = now() "
                    "WHERE username = $1",
                    pqxx::params{user.username});
            tx.commit();
            return VerifyResult::kOk;
        }

        // Wrong password: increment, and lock in the same statement when the
        // increment reaches the threshold. One statement because a read-then-
        // write would let two concurrent failures both see 4 and each write 5.
        const auto rows = tx.exec(
            "UPDATE users SET "
            "  failed_attempts = failed_attempts + 1, "
            "  locked_until = CASE WHEN failed_attempts + 1 >= $2 "
            "                      THEN now() + $3::interval ELSE locked_until END, "
            "  updated_at = now() "
            "WHERE username = $1 "
            "RETURNING failed_attempts, locked_until IS NOT NULL AS locked",
            pqxx::params{user.username, kMaxAttempts, kLockInterval});
        tx.commit();
        if (!rows.empty() && rows[0]["locked"].as<bool>()) {
            return VerifyResult::kLocked;
        }
        return VerifyResult::kWrongPassword;
    } catch (const pqxx::failure&) {
        // A database failure must not be reported as a wrong password *and* must
        // not accept the login. kLocked renders as the generic 401, which is the
        // honest outcome: the server could not verify, so it did not authenticate.
        return VerifyResult::kLocked;
    }
}

LockoutState PgUserStore::Lockout(const std::string& username) {
    LockoutState state;
    try {
        pqxx::connection conn(db_url_);
        pqxx::read_transaction tx(conn);
        const auto rows = tx.exec(
            "SELECT failed_attempts, "
            "       CASE WHEN locked_until > now() THEN to_char(locked_until AT TIME ZONE 'UTC', "
            "                 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') ELSE '' END AS locked_until "
            "FROM users WHERE username = $1",
            pqxx::params{username});
        if (rows.empty()) {
            return state;
        }
        state.failed_attempts = rows[0]["failed_attempts"].as<int>();
        state.locked_until_iso8601 = rows[0]["locked_until"].as<std::string>();
    } catch (const pqxx::failure&) {
        return LockoutState{};
    }
    return state;
}

Session PgUserStore::IssueSession(const std::string& username) {
    Session session;
    try {
        // 32 random bytes, hex-encoded. The token is returned to the client and
        // never stored; only its sha256 is persisted, so a database dump yields
        // no usable credential.
        const auto token = password::GenerateToken();
        if (!token.has_value()) {
            return Session{};
        }

        // HashToken, not Hash: sessions.token_hash is plain sha256(token), and
        // SessionStore::Resolve uses the same helper to look it up.
        const auto token_hash = password::HashToken(*token);
        if (!token_hash.has_value()) {
            return Session{};
        }

        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);
        const auto rows = tx.exec(
            "INSERT INTO sessions (token_hash, username, expires_at) "
            "VALUES ($1, $2, now() + interval '8 hours') "
            "RETURNING to_char(expires_at AT TIME ZONE 'UTC', "
            "                  'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS expires_at",
            pqxx::params{*token_hash, username});
        // Read the row before committing: `rows` is a result tied to this
        // transaction, and reading it after commit() relies on libpqxx keeping
        // the buffer alive, which is not part of the contract.
        session.expires_at_iso8601 = rows[0]["expires_at"].as<std::string>();
        tx.commit();

        session.token = *token;
    } catch (const pqxx::failure&) {
        return Session{};
    }
    return session;
}

}  // namespace srs