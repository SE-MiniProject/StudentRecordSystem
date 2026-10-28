#include "auth/session_store.h"

namespace srs {
namespace {

// Rejects every token. Same rationale as DefaultUserStore: the endpoint stays
// reachable and correctly shaped before the sessions table exists, and no
// request is ever treated as authenticated by accident.
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
    static RejectingSessionStore store;
    return store;
}

}  // namespace srs