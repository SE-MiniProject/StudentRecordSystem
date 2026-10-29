#include "auth/session_store.h"

#include <cstdlib>
#include <optional>
#include <string>

#include "auth/pg_session_store.h"

namespace srs {
namespace {

// The store used when DATABASE_URL is unset. It rejects every token, so every
// authenticated route returns 401: reachable and correctly shaped, without
// pretending to have authenticated anyone.
//
// Tests inject their own store through RegisterLogoutRoute, so this path is only
// reached by a running server, and PgSessionStore takes over the moment a URL is
// configured.
class RejectingSessionStore final : public SessionStore {
public:
    std::optional<SessionIdentity> Resolve(const std::string&) override {
        return std::nullopt;
    }

    bool Revoke(const std::string&) override {
        return false;
    }
};

}  // namespace

SessionStore& DefaultSessionStore() {
    // Same shape as DefaultUserStore: a function-local static so the store
    // outlives the server and is built once, with DATABASE_URL read here because
    // this is the only place that decides which implementation is in use.
    static SessionStore& store = []() -> SessionStore& {
        const char* url = std::getenv("DATABASE_URL");
        if (url != nullptr && *url != '\0') {
            static PgSessionStore pg(url);
            return pg;
        }
        static RejectingSessionStore rejecting;
        return rejecting;
    }();
    return store;
}

}  // namespace srs