#include "auth/user_store.h"

#include <cstdlib>
#include <optional>
#include <string>

#include "auth/pg_user_store.h"

namespace srs {
namespace {

// The store used when DATABASE_URL is unset. It finds nothing, so every login
// returns the generic 401. That is the honest failure for a server with no
// database: the endpoint is reachable and shaped correctly, and no credential is
// ever accepted by accident.
//
// Tests inject their own store through RegisterLoginRoute, so this path is only
// reached by a running server, and PgUserStore takes over the moment a URL is
// configured.
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
    // A function-local static so the store outlives the server and is built once.
    // DATABASE_URL is read here rather than in the caller because this is the
    // only place that decides which implementation is in use.
    static UserStore& store = []() -> UserStore& {
        const char* url = std::getenv("DATABASE_URL");
        if (url != nullptr && *url != '\0') {
            static PgUserStore pg(url);
            return pg;
        }
        static UnbackedUserStore unbacked;
        return unbacked;
    }();
    return store;
}

}  // namespace srs