#include "auth/pg_session_store.h"

#include <pqxx/pqxx>

#include <optional>
#include <string>
#include <utility>

#include "auth/password_hash.h"

namespace srs {
namespace {

// The idle window. api.md: `last_seen_at` is the idle timeout and `expires_at`
// the absolute one; this is the idle half, and it lives here because it is a
// property of the stored session, not of any route.
constexpr const char* kIdleInterval = "30 minutes";

}  // namespace

PgSessionStore::PgSessionStore(std::string db_url) : db_url_(std::move(db_url)) {}

std::optional<SessionIdentity> PgSessionStore::Resolve(const std::string& token) {
    if (token.empty()) {
        return std::nullopt;
    }
    const auto token_hash = password::HashToken(token);
    if (!token_hash.has_value()) {
        return std::nullopt;
    }

    try {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);

        // Resolve and touch in one statement. The JOIN reads the role from the
        // account rather than trusting a stored copy, so a role change takes
        // effect on the next request instead of waiting for re-login.
        //
        // Both expiry conditions are in the WHERE, so a lapsed row is simply not
        // found: the contract says an expired session is invalid, and every
        // route's response to "invalid" is identical.
        const auto rows = tx.exec(
            "UPDATE sessions s SET last_seen_at = now() "
            "FROM users u "
            "WHERE s.token_hash = $1 AND s.username = u.username "
            "  AND s.expires_at > now() "
            "  AND s.last_seen_at > now() - $2::interval "
            "RETURNING s.username, u.role",
            pqxx::params{*token_hash, kIdleInterval});
        tx.commit();

        if (rows.empty()) {
            return std::nullopt;
        }
        SessionIdentity identity;
        identity.username = rows[0]["username"].as<std::string>();
        identity.role = rows[0]["role"].as<std::string>();
        return identity;
    } catch (const pqxx::failure&) {
        // No database, no session. Resolving to nullopt renders as 401, which is
        // the honest answer: the server could not confirm the token.
        return std::nullopt;
    }
}

bool PgSessionStore::Revoke(const std::string& token) {
    if (token.empty()) {
        return false;
    }
    const auto token_hash = password::HashToken(token);
    if (!token_hash.has_value()) {
        return false;
    }

    try {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);
        // Deletes regardless of expiry: an expired row is already invalid, but
        // removing it is still the right cleanup, and the return value must
        // report whether a *live* session was revoked so logout can answer
        // 204 vs 401.
        const auto rows = tx.exec(
            "DELETE FROM sessions WHERE token_hash = $1 "
            "AND expires_at > now() AND last_seen_at > now() - $2::interval "
            "RETURNING token_hash",
            pqxx::params{*token_hash, kIdleInterval});
        tx.commit();
        return !rows.empty();
    } catch (const pqxx::failure&) {
        return false;
    }
}

}  // namespace srs