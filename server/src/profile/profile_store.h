#pragma once

#include <optional>
#include <string>

namespace srs {

// The signed-in caller as the client needs to know them after login: who they
// are, their role, and — for a Student — which student record is theirs.
//
// rollNo is what lets the web client ask for "my grades" without the user typing
// a roll number. The login response does not carry it (api.md "Auth" is frozen),
// so GET /api/me exposes it instead. It is never used for authorisation: the
// routes resolve ownership again from users.roll_no on every request.
struct Profile {
    std::string username;
    std::string full_name;
    std::string role;
    std::string roll_no;  // empty for admin and teacher accounts
};

// Storage behind GET /api/me. An interface so the route is testable with a fake,
// matching every other store in the server.
class ProfileStore {
public:
    virtual ~ProfileStore() = default;

    // The profile for `username`, or nullopt when the account does not exist.
    virtual std::optional<Profile> Find(const std::string& username) = 0;
};

// Postgres-backed when DATABASE_URL is set; otherwise every lookup is empty.
ProfileStore& DefaultProfileStore();

}  // namespace srs
