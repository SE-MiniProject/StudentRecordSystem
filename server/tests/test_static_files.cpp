#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "api/router.h"
#include "server_fixture.h"
#include "web/static_files.h"

namespace {

namespace fs = std::filesystem;

// Builds a throwaway web root so the test does not depend on the repository's
// web/ tree having a particular file in it, and binds the static routes alone on
// a fresh server: that is the smallest unit that can fail.
class StaticOnlyFixture : public srs::test::ServerFixture {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("srs_static_only_" + std::to_string(::getpid()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "js");

        std::ofstream(root_ / "index.html") << "<!doctype html><title>srs</title>";
        std::ofstream(root_ / "js" / "app.js") << "console.log('srs');";
        std::ofstream(root_ / "notes.dat") << "unknown extension";

        sibling_ = root_.parent_path() / (root_.filename().string() + "-backup");
        fs::create_directories(sibling_);
        std::ofstream(sibling_ / "secret.txt") << "outside the web root";

        srs::test::ServerFixture::SetUp();
    }

    void TearDown() override {
        srs::test::ServerFixture::TearDown();
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::remove_all(sibling_, ec);
    }

    void configure(httplib::Server& server) override {
        srs::RegisterStaticRoutes(server, root_.string());
        // The production error handler, so a missing asset produces the same
        // JSON envelope a browser or client actually sees.
        server.set_error_handler(
            [](const httplib::Request& req, httplib::Response& res) -> httplib::Server::HandlerResponse {
                if (!res.body.empty()) return httplib::Server::HandlerResponse::Unhandled;
                nlohmann::json body{{"error", "not_found"},
                                    {"message", "no route for " + req.method + " " + req.path},
                                    {"requestId", req.get_header_value("X-Request-Id")}};
                res.set_content(body.dump(), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            });
    }

    fs::path root_;
    fs::path sibling_;
};

TEST_F(StaticOnlyFixture, RootServesIndexHtml) {
    const auto res = Get("/");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "text/html; charset=utf-8");
    EXPECT_NE(res->body.find("<!doctype html>"), std::string::npos);
}

TEST_F(StaticOnlyFixture, NestedAssetServesWithJavaScriptMime) {
    const auto res = Get("/js/app.js");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 200);
    // A .js served as text/plain is refused by the browser under nosniff, so the
    // exact type is part of the behaviour, not a detail.
    EXPECT_EQ(res->get_header_value("Content-Type"), "text/javascript; charset=utf-8");
    EXPECT_EQ(res->body, "console.log('srs');");
}

TEST_F(StaticOnlyFixture, MissingAssetIs404WithJsonEnvelope) {
    const auto res = httplib::Client(base_url()).Get("/js/missing.js", {{"X-Request-Id", "req-9"}});

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "not_found");
    EXPECT_EQ(body.at("requestId"), "req-9");
}

TEST_F(StaticOnlyFixture, UnknownExtensionIsRefused) {
    // Serving an unrecognised type as octet-stream makes the browser download
    // it instead of rendering it, so it is treated as absent. Placed under js/
    // because that is a route the client actually serves from; a top-level
    // /notes.dat does not match any static route and 404s for a different
    // reason, which would make this test pass without testing the MIME check.
    fs::create_directories(root_ / "js");
    std::ofstream(root_ / "js" / "notes.dat") << "binary";
    const auto res = Get("/js/notes.dat");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 404);
}

TEST_F(StaticOnlyFixture, DotDotTraversalIsRefused) {
    // Under /js/ so the request reaches a static route at all; the containment
    // check is what must stop it, not the absence of a matching route.
    const auto res = httplib::Client(base_url()).Get("/js/../../../../etc/passwd");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(res->body.find("root:"), std::string::npos);
}

TEST_F(StaticOnlyFixture, SiblingDirectoryWithSharedPrefixIsRefused) {
    // root is ".../srs_static_only_1234"; the sibling is "...-backup". A
    // prefix-based containment check would serve this file.
    const auto res =
        httplib::Client(base_url()).Get("/js/../../" + sibling_.filename().string() + "/secret.txt");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(res->body.find("outside the web root"), std::string::npos);
}

TEST_F(StaticOnlyFixture, NonAssetPathIsNotClaimedByStaticRoutes) {
    // The regression this guards: a blanket `/(.+)` catch-all matched every GET,
    // so a route registered after RegisterRoutes -- and any unrouted /api path --
    // got an empty 200 instead of reaching the error handler.
    const auto res = httplib::Client(base_url()).Get("/boom", {{"X-Request-Id", "r-1"}});

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "not_found");
    EXPECT_EQ(body.at("message"), "no route for GET /boom");
}

TEST_F(StaticOnlyFixture, UnknownApiPathKeepsJsonEnvelope) {
    // An unrouted API path must stay a JSON 404. If a static route claimed it,
    // the browser would receive HTML and the client's error mapping would break.
    const auto res = Get("/api/no-such-endpoint");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const auto body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body.at("error"), "not_found");
}

TEST_F(StaticOnlyFixture, DotDotCannotEscapeTheRouteSubdirectory) {
    // Regression: containment used to be checked against the web root, so
    // `/js/../package.json` resolved to <root>/package.json and was served --
    // a file outside /js/, through a route that only claims /js/. The earlier
    // traversal tests missed it because they escaped the root entirely (which
    // was already refused); this escapes only the route's subdirectory.
    std::ofstream(root_ / "package.json") << "{}";

    const auto res = httplib::Client(base_url()).Get("/js/../package.json");

    ASSERT_TRUE(res) << httplib::to_string(res.error());
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(res->body.find("{}"), std::string::npos);
}

TEST_F(StaticOnlyFixture, WebRootFromEnvFallsBackToDefault) {
    ::unsetenv(srs::kWebRootEnvVar);
    EXPECT_EQ(srs::WebRootFromEnv(), std::string(srs::kDefaultWebRoot));

    ::setenv(srs::kWebRootEnvVar, "/tmp/some-root", 1);
    EXPECT_EQ(srs::WebRootFromEnv(), std::string("/tmp/some-root"));

    ::unsetenv(srs::kWebRootEnvVar);
    EXPECT_EQ(srs::WebRootFromEnv(), std::string(srs::kDefaultWebRoot));
}

TEST_F(StaticOnlyFixture, WebRootExistsDetectsDirectoriesOnly) {
    EXPECT_TRUE(srs::WebRootExists(root_.string()));
    EXPECT_FALSE(srs::WebRootExists((root_ / "index.html").string()));
    EXPECT_FALSE(srs::WebRootExists((root_ / "definitely-absent").string()));
}

}  // namespace