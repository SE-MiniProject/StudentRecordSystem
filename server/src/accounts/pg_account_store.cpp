#include "accounts/pg_account_store.h"

#include <pqxx/pqxx>

#include <cstdlib>
#include <string>
#include <vector>

#include "auth/password_hash.h"

namespace srs {
namespace {

// The store used when DATABASE_URL is unset: no accounts, no writes. The routes
// stay reachable and correctly shaped without a database, and a create never
// appears to succeed against storage that is not there.
class UnbackedAccountStore final : public AccountStore {
public:
    std::optional<std::vector<Account>> List() override { return std::vector<Account>{}; }
    CreateAccountResult Create(const std::string&, const std::string&, const std::string&,
                               const std::string&) override {
        return CreateAccountResult::kError;
    }
    PatchAccountResult Patch(const std::string&, const AccountChanges&) override {
        return PatchAccountResult::kError;
    }
};

Account Decode(const pqxx::row_ref& row) {
    Account account;
    account.username = row["username"].as<std::string>();
    account.full_name = row["full_name"].as<std::string>();
    account.role = row["role"].as<std::string>();
    account.active = row["active"].as<bool>();
    return account;
}

}  // namespace

PgAccountStore::PgAccountStore(std::string db_url) : db_url_(std::move(db_url)) {}

std::optional<std::vector<Account>> PgAccountStore::List() {
    try {
        pqxx::connection conn(db_url_);
        pqxx::read_transaction tx(conn);
        // Explicit column list, not SELECT *: the digest and salt columns must
        // not be read into a process that has no use for them, because that is
        // how they end up in a log line.
        const auto rows = tx.exec(
            "SELECT username, full_name, role, active FROM users ORDER BY username");
        std::vector<Account> accounts;
        accounts.reserve(rows.size());
        for (const auto& row : rows) accounts.push_back(Decode(row));
        return accounts;
    } catch (const pqxx::failure&) {
        return std::nullopt;
    }
}

CreateAccountResult PgAccountStore::Create(const std::string& username,
                                           const std::string& password,
                                           const std::string& full_name,
                                           const std::string& role) {
    // Hashing happens before the connection is opened so a CSPRNG failure does
    // not leave a half-built transaction behind.
    const auto salt = password::GenerateSalt();
    if (!salt.has_value()) return CreateAccountResult::kError;
    const auto digest = password::Hash(password, *salt);
    if (!digest.has_value()) return CreateAccountResult::kError;

    try {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);
        tx.exec(
            "INSERT INTO users (username, full_name, role, password_hash, password_salt, "
            "password_algo) VALUES ($1, $2, $3, $4, $5, 'sha256')",
            pqxx::params{username, full_name, role, *digest, *salt});
        tx.commit();
        return CreateAccountResult::kOk;
    } catch (const pqxx::unique_violation&) {
        return CreateAccountResult::kDuplicateUsername;
    } catch (const pqxx::failure&) {
        return CreateAccountResult::kError;
    }
}

PatchAccountResult PgAccountStore::Patch(const std::string& username,
                                         const AccountChanges& changes) {
    // A new password is rehashed with a fresh salt before touching the database.
    std::string new_hash;
    std::string new_salt;
    if (!changes.password.empty()) {
        const auto salt = password::GenerateSalt();
        if (!salt.has_value()) return PatchAccountResult::kError;
        const auto digest = password::Hash(changes.password, *salt);
        if (!digest.has_value()) return PatchAccountResult::kError;
        new_hash = *digest;
        new_salt = *salt;
    }

    try {
        pqxx::connection conn(db_url_);
        pqxx::work tx(conn);

        // COALESCE per column, same shape as the student update: an empty value
        // means "unchanged". active is a nullable bool because false is a real
        // value that must be distinguishable from "not supplied".
        const auto rows = tx.exec(
            "UPDATE users SET "
            "  full_name = COALESCE(NULLIF($2, ''), full_name), "
            "  role = COALESCE(NULLIF($3, ''), role), "
            "  active = COALESCE($4::boolean, active), "
            "  password_hash = COALESCE(NULLIF($5, ''), password_hash), "
            "  password_salt = COALESCE(NULLIF($6, ''), password_salt), "
            "  updated_at = now() "
            "WHERE username = $1 RETURNING username",
            pqxx::params{username, changes.full_name, changes.role,
                         changes.active.has_value() ? std::optional<bool>(*changes.active)
                                                    : std::nullopt,
                         new_hash, new_salt});
        if (rows.empty()) {
            tx.abort();
            return PatchAccountResult::kNotFound;
        }

        // Deactivating must take effect immediately, not at token expiry: the
        // contract says setting active false "revokes the user's sessions and
        // blocks login" (F-006). Doing it in the same transaction means a
        // successful PATCH cannot leave a live session behind.
        if (changes.active.has_value() && !*changes.active) {
            tx.exec("DELETE FROM sessions WHERE username = $1", pqxx::params{username});
        }

        tx.commit();
        return PatchAccountResult::kOk;
    } catch (const pqxx::failure&) {
        return PatchAccountResult::kError;
    }
}

AccountStore& DefaultAccountStore() {
    static AccountStore& store = []() -> AccountStore& {
        const char* url = std::getenv("DATABASE_URL");
        if (url != nullptr && *url != '\0') {
            static PgAccountStore pg(url);
            return pg;
        }
        static UnbackedAccountStore unbacked;
        return unbacked;
    }();
    return store;
}

}  // namespace srs