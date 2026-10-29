#pragma once

#include <string>

#include "accounts/account_store.h"

namespace srs {

class PgAccountStore final : public AccountStore {
public:
    explicit PgAccountStore(std::string db_url);

    std::optional<std::vector<Account>> List() override;
    CreateAccountResult Create(const std::string& username, const std::string& password,
                               const std::string& full_name, const std::string& role) override;
    PatchAccountResult Patch(const std::string& username, const AccountChanges& changes) override;

private:
    std::string db_url_;
};

}  // namespace srs