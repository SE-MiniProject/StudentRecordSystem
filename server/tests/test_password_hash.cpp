#include <gtest/gtest.h>

#include <string>

#include "auth/password_hash.h"

namespace {

TEST(PasswordHash, GeneratesDistinctHexSaltAndBearerTokens) {
    const auto salt_a = srs::password::GenerateSalt();
    const auto salt_b = srs::password::GenerateSalt();
    const auto token_a = srs::password::GenerateToken();
    const auto token_b = srs::password::GenerateToken();

    ASSERT_TRUE(salt_a.has_value());
    ASSERT_TRUE(salt_b.has_value());
    ASSERT_TRUE(token_a.has_value());
    ASSERT_TRUE(token_b.has_value());
    EXPECT_EQ(salt_a->size(), 32u);
    EXPECT_EQ(salt_b->size(), 32u);
    EXPECT_NE(*salt_a, *salt_b);
    EXPECT_EQ(token_a->size(), 64u);
    EXPECT_EQ(token_b->size(), 64u);
    EXPECT_NE(*token_a, *token_b);
    EXPECT_NE(*salt_a, *token_a);
    for (const auto* value : {&*salt_a, &*salt_b, &*token_a, &*token_b}) {
        EXPECT_TRUE(value->find_first_not_of("0123456789abcdef") == std::string::npos);
    }
}

TEST(PasswordHash, HashRejectsOddLengthAndNonHexSalts) {
    EXPECT_FALSE(srs::password::Hash("pw", "0").has_value());
    EXPECT_FALSE(srs::password::Hash("pw", "0g").has_value());
    EXPECT_FALSE(srs::password::Hash("pw", "G0").has_value());
    EXPECT_FALSE(srs::password::Hash("pw", "0-").has_value());
}

TEST(PasswordHash, HashUsesDecodedSaltAndPasswordBytes) {
    const auto digest = srs::password::Hash("password", "00112233445566778899aabbccddeeff");
    ASSERT_TRUE(digest.has_value());
    EXPECT_EQ(digest->size(), 64u);
    EXPECT_EQ(*digest, "9db024047938d4cad75a5684778b36d7f38b7f792ad69fd1a8aebe48bdc011eb");
    EXPECT_EQ(srs::password::Hash("password", "00112233445566778899AABBCCDDEEFF"), digest);
    EXPECT_NE(srs::password::Hash("password", "10112233445566778899aabbccddeeff"), digest);
    EXPECT_NE(srs::password::Hash("Password", "00112233445566778899aabbccddeeff"), digest);
}

TEST(PasswordHash, ConstantTimeComparisonHandlesLengthAndContentDifferences) {
    EXPECT_TRUE(srs::password::ConstantTimeEqual("abcd", "abcd"));
    EXPECT_FALSE(srs::password::ConstantTimeEqual("abcd", "abce"));
    EXPECT_FALSE(srs::password::ConstantTimeEqual("abcd", "abc"));
    EXPECT_TRUE(srs::password::ConstantTimeEqual("", ""));
}

TEST(PasswordHash, TokenHashIsStableSha256OfInput) {
    const auto digest = srs::password::HashToken("token");
    ASSERT_TRUE(digest.has_value());
    EXPECT_EQ(*digest, "3c469e9d6c5875d37a43f353d4f88e61fcf812c66eee3457465a40b0da4153e0");
    EXPECT_NE(srs::password::HashToken("other"), digest);
}

}  // namespace
