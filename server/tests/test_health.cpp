#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <thread>

#include "api/router.h"

namespace {

// Binds the real routes to port 0 so the OS picks a free port. Each test gets
// its own server, so tests can run in any order and in parallel.
class ServerFixture : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<httplib::Server>();
        srs::RegisterRoutes(*server_);
        RegisterTestRoutes();

        port_ = server_->bind_to_any_port("127.0.0.1");
        ASSERT_GT(port_, 0) << "could not bind to an ephemeral port";

        thread_ = std::thread([this] { server_->listen_after_bind(); });
        // Blocks until the listener is accepting, so a failed bind surfaces on
        // the first request rather than here.
        server_->wait_until_ready();
    }

    void TearDown() override {
        server_->stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    // The server has no route that fails on purpose, so one is added here to
    // drive the error handler on a status other than 404.
    void RegisterTestRoutes() {
        server_->Get("/boom", [](const httplib::Request&, httplib::Response& res) {
            res.status = 500;
        });
    }

    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    int port_ = -1;
};

}  // namespace

TEST_F(ServerFixture, HealthReturnsOkJson) {
    const auto res = httplib::Client(base_url()).Get("/health");

    ASSERT_TRUE(res) << "request failed: " << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("status"), "ok");
    EXPECT_EQ(body.at("service"), "srs_server");
    EXPECT_TRUE(body.contains("version"));
}

TEST_F(ServerFixture, UnknownPathReturns404WithRequestId) {
    const auto res = httplib::Client(base_url()).Get("/nope", {{"X-Request-Id", "abc-123"}});

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "not_found");
    EXPECT_EQ(body.at("message"), "no route for GET /nope");
    EXPECT_EQ(body.at("requestId"), "abc-123");
}

TEST_F(ServerFixture, WrongVerbOnKnownPathNamesTheVerb) {
    const auto res = httplib::Client(base_url()).Post("/health", "{}", "application/json");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("message"), "no route for POST /health");
}

TEST_F(ServerFixture, NonNotFoundErrorAlsoReturnsJsonEnvelope) {
    const auto res = httplib::Client(base_url()).Get("/boom", {{"X-Request-Id", "err-1"}});

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 500);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "error");
    EXPECT_EQ(body.at("message"), "Internal Server Error");
    EXPECT_EQ(body.at("requestId"), "err-1");
}