#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <string>

#include "server_fixture.h"

namespace {

// Regression guard for a bug the login tests could not see: they register only
// the login route, so no global error handler is installed. In the real server
// RegisterRoutes also installs one, and httplib calls it for *every* response
// with status >= 400. If that handler unconditionally rewrites the body, login's
// specific {"error":"unauthenticated"} JSON is replaced by {"error":"error"}.
//
// The fixture's default configure() builds the server through RegisterRoutes,
// exactly like main.cpp, so a route body and the global handler are both in play.
using ErrorHandlerFixture = srs::test::ServerFixture;

}  // namespace

// The unbacked store rejects every login. The point here is not the 401 but the
// body: it must be login's own unauthenticated envelope, not the generic one.
TEST_F(ErrorHandlerFixture, RouteAuthored401BodySurvivesGlobalErrorHandler) {
    const auto res = Post("/api/auth/login", R"({"username":"a.shetty","password":"x"})", "live-1");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 401);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "unauthenticated");
    EXPECT_EQ(body.at("message"), "invalid username or password");
    EXPECT_EQ(body.at("requestId"), "live-1");
}

TEST_F(ErrorHandlerFixture, RouteAuthored400BodySurvivesGlobalErrorHandler) {
    const auto res = Post("/api/auth/login", R"({"username":"a.shetty"})", "live-2");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "validation_failed");
    EXPECT_EQ(body.at("fields").at("password"), "required");
    EXPECT_EQ(body.at("requestId"), "live-2");
}

// The global handler must still do its own job for responses no route authored.
TEST_F(ErrorHandlerFixture, UnroutedRequestStillGetsGlobalEnvelope) {
    const auto res = Get("/nope");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "not_found");
    EXPECT_EQ(body.at("message"), "no route for GET /nope");
}

TEST_F(ErrorHandlerFixture, WrongVerbOnKnownPathStillGetsGlobalEnvelope) {
    const auto res = Post("/health", R"({})", std::string());

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "not_found");
    EXPECT_EQ(body.at("message"), "no route for POST /health");
}
