#pragma once

#include <optional>
#include <string>
#include <vector>

namespace srs {

// One account as the Admin views it. Deliberately carries no digest, salt or
// token: the contract says GET /api/accounts never returns them, and a type that
// cannot hold them cannot leak them (api.md line 208).
struct Account {
    std::string username;
    std::string full_name;
    std::string role;
    bool active = true;
};

// The outcome of a create (SRS-19). Duplicate usernames answer 409, so they are
// a distinct result rather than an error.
enum class CreateAccountResult {
    kOk,
    kDuplicateUsername,
    kError,
};

// The outcome of a PATCH (SRS-17).
enum class PatchAccountResult {
    kOk,
    kNotFound,
    kError,
};

// A partial account update. Empty strings / nullopt mean "leave unchanged",
// matching the contract's "all optional" body.
struct AccountChanges {
    std::string full_name;
    std::string role;
    std::optional<bool> active;
    std::string password;  // plaintext in, hashed by the store; never stored raw
};

// Storage behind the account routes. Same seam reasoning as UserStore: the
// routes own the contract, the store owns the columns.
class AccountStore {
public:
    virtual ~AccountStore() = default;

    virtual std::optional<std::vector<Account>> List() = 0;
    virtual CreateAccountResult Create(const std::string& username, const std::string& password,
                                       const std::string& full_name, const std::string& role) = 0;
    virtual PatchAccountResult Patch(const std::string& username,
                                     const AccountChanges& changes) = 0;
};

AccountStore& DefaultAccountStore();

}  // namespace srs