#include <pqxx/pqxx>

#include <cstdlib>
#include <string>

#include "profile/profile_store.h"

namespace srs {
namespace {

class PgProfileStore final : public ProfileStore {
public:
    explicit PgProfileStore(std::string db_url) : db_url_(std::move(db_url)) {}

    std::optional<Profile> Find(const std::string& username) override {
        try {
            pqxx::connection conn(db_url_);
            pqxx::read_transaction tx(conn);
            // Only the columns a profile may show. The password columns are not
            // selected, so they cannot reach a response by accident.
            const auto rows = tx.exec(
                "SELECT username, full_name, role, COALESCE(roll_no, '') AS roll_no "
                "FROM users WHERE username = $1",
                pqxx::params{username});
            if (rows.empty()) return std::nullopt;
            Profile profile;
            profile.username = rows[0]["username"].as<std::string>();
            profile.full_name = rows[0]["full_name"].as<std::string>();
            profile.role = rows[0]["role"].as<std::string>();
            profile.roll_no = rows[0]["roll_no"].as<std::string>();
            return profile;
        } catch (const pqxx::failure&) {
            return std::nullopt;
        }
    }

private:
    std::string db_url_;
};

class UnbackedProfileStore final : public ProfileStore {
public:
    std::optional<Profile> Find(const std::string&) override { return std::nullopt; }
};

}  // namespace

ProfileStore& DefaultProfileStore() {
    static ProfileStore& store = []() -> ProfileStore& {
        const char* url = std::getenv("DATABASE_URL");
        if (url != nullptr && *url != '\0') {
            static PgProfileStore pg(url);
            return pg;
        }
        static UnbackedProfileStore unbacked;
        return unbacked;
    }();
    return store;
}

}  // namespace srs
