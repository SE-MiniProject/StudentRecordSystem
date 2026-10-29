#pragma once

#include <string>

#include "auth/session_store.h"

namespace srs {

// The Postgres-backed SessionStore. Resolves a bearer token by hashing it and
// looking the digest up in `sessions`, which is the same construction
// PgUserStore::IssueSession used to store it.
//
// A row is honoured only while both expiries hold: the absolute one
// (`expires_at`) and the idle one (`last_seen_at`). api.md defines both, so a
// session that has sat untouched for the idle window is invalid even before its
// absolute expiry.
class PgSessionStore final : public SessionStore {
public:
    explicit PgSessionStore(std::string db_url);

    std::optional<SessionIdentity> Resolve(const std::string& token) override;
    bool Revoke(const std::string& token) override;

private:
    std::string db_url_;
};

}  // namespace srs