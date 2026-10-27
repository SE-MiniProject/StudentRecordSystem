#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "api/router.h"
#include "server_fixture.h"

namespace {

// The production routes plus one that fails on purpose: the server has no route
// that errors deliberately, and the error handler's non-404 branch needs one to
// drive it.
class HealthFixture : public srs::test::ServerFixture {
protected:
    void configure(httplib::Server& server) override {
        srs::RegisterRoutes(server);
        server.Get("/boom", [](const httplib::Request&, httplib::Response& res) {
            res.status = 500;
        });
    }
};

}  // namespace

TEST_F(HealthFixture, HealthReturnsOkJson) {
    const auto res = Get("/health");

    ASSERT_TRUE(res) << "request failed: " << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("status"), "ok");
    EXPECT_EQ(body.at("service"), "srs_server");
    EXPECT_TRUE(body.contains("version"));
}

TEST_F(HealthFixture, UnknownPathReturns404WithRequestId) {
    const auto res = httplib::Client(base_url()).Get("/nope", {{"X-Request-Id", "abc-123"}});

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "not_found");
    EXPECT_EQ(body.at("message"), "no route for GET /nope");
    EXPECT_EQ(body.at("requestId"), "abc-123");
}

TEST_F(HealthFixture, WrongVerbOnKnownPathNamesTheVerb) {
    const auto res = Post("/health", "{}", std::string());

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("message"), "no route for POST /health");
}

TEST_F(HealthFixture, NonNotFoundErrorAlsoReturnsJsonEnvelope) {
    const auto res = httplib::Client(base_url()).Get("/boom", {{"X-Request-Id", "err-1"}});

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 500);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "error");
    EXPECT_EQ(body.at("message"), "Internal Server Error");
    EXPECT_EQ(body.at("requestId"), "err-1");
}
