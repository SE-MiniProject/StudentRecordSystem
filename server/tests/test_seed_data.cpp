#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "auth/password_hash.h"

namespace srs {
namespace {

// The demo seed is a fixture other people rely on: an end-to-end login test and
// anyone running the server locally log in as one of these three accounts. A
// mismatch between a row's salt and its digest is invisible at seed time — the
// rows insert happily and every login is then rejected as a wrong password,
// which reads like an auth bug rather than a data bug.
//
// So this test parses db/seeds/demo_data.sql and recomputes every digest from
// the password and salt in the same row. It needs no database, which is what
// lets it run on every build instead of only where Postgres is available.

struct SeedRow {
    std::string username;
    std::string password_hash;
    std::string password_salt;
};

// The fixture passwords, keyed by username. Kept beside the parser rather than
// in the seed file because the seed deliberately stores no plaintext.
const char* PasswordFor(const std::string& username) {
    if (username == "admin") return "admin";
    if (username == "teacher") return "teacher";
    if (username == "student") return "student";
    return nullptr;
}

std::string ReadSeedFile() {
    // The path is baked in by CMake rather than guessed from the working
    // directory: ctest's CWD is the build tree, whose depth is not a property
    // this test should depend on.
    std::ifstream in(SRS_SEED_FILE);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Pulls (username, hash, salt) triples out of the INSERT ... VALUES block. The
// file is hand-written SQL with fixed formatting, so a targeted scan is honest
// here; a real SQL parser would be the wrong amount of machinery for a fixture.
std::vector<SeedRow> ParseSeedRows(const std::string& sql) {
    std::vector<SeedRow> rows;
    const std::size_t values_at = sql.find("VALUES");
    if (values_at == std::string::npos) return rows;

    // Every quoted literal after VALUES, in file order, is either a username,
    // full_name, role, hash, salt or algo. The pattern per row is exactly:
    //   'user', 'full name', 'role', 'hash', 'salt', 'algo'
    constexpr std::size_t kLiteralsPerRow = 6;
    std::vector<std::string> literals;
    std::size_t i = values_at;
    while ((i = sql.find('\'', i)) != std::string::npos) {
        const std::size_t end = sql.find('\'', i + 1);
        if (end == std::string::npos) break;
        literals.push_back(sql.substr(i + 1, end - i - 1));
        i = end + 1;
    }

    for (std::size_t k = 0; k + kLiteralsPerRow - 1 < literals.size(); k += kLiteralsPerRow) {
        SeedRow row;
        row.username = literals[k + 0];
        row.password_hash = literals[k + 3];
        row.password_salt = literals[k + 4];
        // Only the users block has sha256 algo in the last slot; the students
        // block that follows does not, so stop at the first non-sha256 row.
        if (literals[k + 5] != "sha256") break;
        rows.push_back(row);
    }
    return rows;
}

TEST(SeedData, FileIsFound) {
    ASSERT_FALSE(ReadSeedFile().empty())
        << "could not locate db/seeds/demo_data.sql from the test working directory";
}

TEST(SeedData, EveryUserRowAuthenticates) {
    const std::string sql = ReadSeedFile();
    ASSERT_FALSE(sql.empty());

    const auto rows = ParseSeedRows(sql);
    ASSERT_EQ(rows.size(), 3u) << "expected the three demo accounts";

    for (const auto& row : rows) {
        const char* password = PasswordFor(row.username);
        ASSERT_NE(password, nullptr) << "unknown demo account: " << row.username;

        const auto digest = password::Hash(password, row.password_salt);
        ASSERT_TRUE(digest.has_value()) << "salt is not valid hex for " << row.username;

        EXPECT_TRUE(password::ConstantTimeEqual(*digest, row.password_hash))
            << "seed digest for '" << row.username
            << "' does not match its salt; regenerate the row with password::Hash";
    }
}

TEST(SeedData, WrongPasswordIsRejected) {
    const std::string sql = ReadSeedFile();
    ASSERT_FALSE(sql.empty());

    for (const auto& row : ParseSeedRows(sql)) {
        const auto digest = password::Hash("definitely-not-the-password", row.password_salt);
        ASSERT_TRUE(digest.has_value());
        EXPECT_FALSE(password::ConstantTimeEqual(*digest, row.password_hash))
            << "seed digest for '" << row.username << "' accepts an arbitrary password";
    }
}

}  // namespace
}  // namespace srs