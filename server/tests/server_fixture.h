#pragma once

#include <gtest/gtest.h>

#include <httplib.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "api/router.h"

namespace srs::test {

// Binds a server to an ephemeral port and runs it on its own thread, so a test
// can talk to it over real HTTP. Each test gets its own server, so tests can run
// in any order and in parallel.
//
// The three test files previously each carried a copy of this lifecycle. It is
// one class because the interesting part of a test is what it asks the server,
// not how the socket is wired.
//
// `configure` is called before the bind: subclasses that need routes registered
// a particular way override it rather than duplicating SetUp. The default
// registers the production routes, exactly like main.cpp.
class ServerFixture : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<httplib::Server>();
        configure(*server_);

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
        // Destroy the server here rather than at fixture scope exit. As a plain
        // member its destructor runs after TearDown returns, which lets the
        // server's teardown overlap the listener thread's frame unwinding; on
        // newer glibc that overlap surfaces as "double free detected in tcache 2"
        // while CMake probes the binary for test discovery. Joining first and
        // releasing here keeps the two strictly ordered.
        server_.reset();
    }

    virtual void configure(httplib::Server& server) {
        srs::RegisterRoutes(server);
    }

    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    httplib::Result Post(const std::string& path, const std::string& body,
                         const std::string& request_id = std::string()) {
        httplib::Headers headers;
        if (!request_id.empty()) {
            headers.emplace("X-Request-Id", request_id);
        }
        return httplib::Client(base_url()).Post(path, headers, body, "application/json");
    }

    // Login convenience overloads. The request id is a named second argument
    // rather than a defaulted trailing one, because Post(path, body) and
    // Post(body, request_id) both take two strings and a call such as
    // Post("/health", "{}") would bind to the wrong route silently.
    httplib::Result PostLogin(const std::string& body) {
        return Post("/api/auth/login", body, std::string());
    }

    httplib::Result PostLogin(const std::string& body, const std::string& request_id) {
        return Post("/api/auth/login", body, request_id);
    }

    httplib::Result Get(const std::string& path) {
        return httplib::Client(base_url()).Get(path);
    }

    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    int port_ = -1;
};

}  // namespace srs::test
