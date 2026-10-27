#include "auth/user_store.h"

#include <optional>
#include <string>

namespace srs {
namespace {

// The store used until the users table is wired up. It finds nothing, so every
// login returns the generic 401. That is the honest failure for a server with no
// database: the endpoint is reachable and shaped correctly, and no credential is
// ever accepted by accident.
//
// Replace the body of these methods with queries against the migration. The
// interface is chosen so that is the only change needed here: the lockout policy
// (counter, threshold, expiry) belongs to this class, which is why it takes
// LockoutState and Session as values rather than exposing them as fields.
class UnbackedUserStore final : public UserStore {
public:
    std::optional<UserRecord> Find(const std::string&) override {
        return std::nullopt;
    }

    VerifyResult VerifyPassword(const UserRecord&, const std::string&) override {
        return VerifyResult::kNoSuchUser;
    }

    LockoutState Lockout(const std::string&) override {
        return LockoutState{};
    }

    Session IssueSession(const std::string&) override {
        return Session{};
    }
};

}  // namespace

UserStore& DefaultUserStore() {
    static UnbackedUserStore store;
    return store;
}

}  // namespace srs
