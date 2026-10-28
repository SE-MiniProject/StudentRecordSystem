#pragma once

#include <optional>
#include <string>

namespace srs {

// The identity behind a bearer token, as resolved from the sessions table. This
// is what every authenticated route needs: who the caller is, and what they may
// do. It deliberately does not carry the password digest — an authenticated
// request has no use for it, and a type that cannot hold it cannot leak it.
struct SessionIdentity {
    std::string username;
    std::string role;
};

// Storage behind every authenticated endpoint: logout today, the RBAC middleware
// next, and each CRUD route after that.
//
// An interface for the same reason UserStore is one: the sessions table is not
// migrated yet, so the routes that consume tokens must compile and be testable
// against a fake. When the table lands, only the implementation changes.
//
// Ownership: BE lane owns this interface; DB lane owns what backs it.
class SessionStore {
public:
    virtual ~SessionStore() = default;

    // Resolves a bearer token to the caller it belongs to. Returns nullopt when
    // the token is unknown, has been revoked, or has lapsed by absolute expiry or
    // idle timeout (SR-008).
    //
    // Lapsed rows are reported as unknown rather than as a third outcome: the
    // contract says an expired session is invalid, and every route's response to
    // "invalid" is identical, so distinguishing them would be a distinction the
    // caller cannot act on.
    virtual std::optional<SessionIdentity> Resolve(const std::string& token) = 0;

    // Revokes a token. Returns true when a live session was deleted, false when
    // the token was already unknown, revoked, or lapsed.
    //
    // The bool is what lets logout answer 204 vs 401 correctly on a second call,
    // without a separate existence check that could race with another logout.
    virtual bool Revoke(const std::string& token) = 0;
};

// The store used when none is injected. Until the sessions table is wired up,
// every token is unknown, so every authenticated route returns 401: reachable and
// correctly shaped, without pretending to have authenticated anyone.
SessionStore& DefaultSessionStore();

}  // namespace srs
