#pragma once

#include <optional>
#include <string>

namespace srs {

// The three hash columns from the users table, kept together because they are
// only ever meaningful as a set: a digest is unverifiable without its salt, and
// uninterpretable without knowing which algorithm produced it. Bundling them
// also gives the hex/algorithm rules somewhere to live (see password_hash.h)
// instead of being restated at each call site as three bare strings.
struct PasswordDigest {
    std::string hash;  // 64 hex chars
    std::string salt;  // 32 hex chars
    std::string algo;  // 'sha256', or 'argon2id' after a migration
};

// One row of the users table, as the login path needs it. Deliberately narrower
// than the table: fields the contract says a response may carry are absent, so
// they cannot leak through a response by accident.
struct UserRecord {
    std::string username;
    std::string full_name;
    std::string role;
    PasswordDigest password;
    bool active = true;
};

// A newly issued session, as the store recorded it. Returned by value rather
// than through an out-parameter: both fields are always produced together, and a
// struct makes that a fact of the type instead of a convention for the caller.
struct Session {
    std::string token;                    // bearer token; only its hash is persisted
    std::string expires_at_iso8601;       // absolute expiry, echoed in the response
};

// The outcome of the password check, as the store reports it. Login needs to
// distinguish "no such user" from "wrong password" internally in order to
// increment the right counter, even though both render as the same 401.
enum class VerifyResult {
    kOk,
    kNoSuchUser,
    kWrongPassword,
    // The account is locked by the failed-attempt policy, or is deactivated.
    // Both are reported to the client as the generic 401.
    kLocked,
};

// What the store knows about a lock after a failed attempt. The route does not
// act on this — the store owns the policy — but a caller (and a test) can read
// it without reaching into the store's internals, and it is what lets a lock be
// represented as something with an end rather than a permanent state.
struct LockoutState {
    int failed_attempts = 0;
    // Empty when the account is not locked. Otherwise the instant the lock
    // expires, ISO-8601 UTC. A non-empty value in the past means the lock has
    // lapsed and the next attempt clears it (the contract's lazy expiry).
    std::string locked_until_iso8601;
};

// Storage behind POST /api/auth/login. An interface rather than direct DB calls
// so the route compiles and is testable before the migration lands: the column
// names and the lockout columns are Utkarsh's to settle, and when they do only
// the implementation behind this seam changes.
//
// Ownership: BE lane (Vishanth) owns this interface; DB lane owns what backs it.
class UserStore {
public:
    virtual ~UserStore() = default;

    // Looks up an account. Returns nullopt when no such username exists. The
    // returned record carries the digest; the caller must never log it.
    virtual std::optional<UserRecord> Find(const std::string& username) = 0;

    // Checks the candidate password against the record and applies the
    // failed-attempt policy: a wrong password increments the counter and a
    // correct one clears it. Returns kLocked once the threshold is reached,
    // without re-checking the password.
    //
    // The store also owns lock expiry: an expired lock is cleared here, so the
    // route never needs a clock and cannot disagree with the store about when a
    // lock ends.
    virtual VerifyResult VerifyPassword(const UserRecord& user, const std::string& password) = 0;

    // The lockout state for `username`, for callers that need to observe the
    // policy (tests, admin views) without duplicating it.
    virtual LockoutState Lockout(const std::string& username) = 0;

    // Issues a session for a verified user. The store persists only a hash of
    // the token, per the sessions table.
    virtual Session IssueSession(const std::string& username) = 0;
};

// The store RegisterRoutes uses when none is injected. Until the migration is
// wired up this rejects every login, so the endpoint is reachable and returns
// the right shape without pretending to authenticate anyone.
UserStore& DefaultUserStore();

}  // namespace srs
