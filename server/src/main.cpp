#include <cstdlib>
#include <iostream>
#include <string>

#include "api/router.h"

namespace {

// Prefer the standard PORT variable used by PaaS/container runtimes.
// Keep SRS_PORT as a backward-compatible fallback for local development.
int PortFromEnv() {
    const char* raw = std::getenv("PORT");
    if (raw == nullptr || *raw == '\0') {
        raw = std::getenv("SRS_PORT");
    }
    if (raw == nullptr || *raw == '\0') {
        return srs::kDefaultPort;
    }

    try {
        const int port = std::stoi(raw);
        if (port <= 0 || port > 65535) {
            std::cerr << "PORT out of range, using " << srs::kDefaultPort << '\n';
            return srs::kDefaultPort;
        }
        return port;
    } catch (const std::exception&) {
        std::cerr << "PORT is not a number, using " << srs::kDefaultPort << '\n';
        return srs::kDefaultPort;
    }
}

// Defaults to 0.0.0.0 ("every IPv4 interface"), which is what container
// runtimes expect. It must NOT default to the empty string: cpp-httplib
// resolves "" to ::1, so the server would listen on IPv6 loopback only and
// every IPv4 client (curl 127.0.0.1, the test scripts, most browsers) would
// get connection refused while the process happily logged "listening".
std::string BindFromEnv() {
    const char* raw = std::getenv("SRS_BIND");
    return raw && *raw ? raw : "0.0.0.0";
}

}  // namespace

// This file is deliberately only an entry point. Every route lives in
// router.cpp's RegisterRoutes, which the tests also call, so a route cannot be
// reachable in production without being reachable from a test. Building a
// second httplib::Server here — as this file used to — meant the tested route
// table and the shipped one were different programs.
int main() {
    const std::string bind = BindFromEnv();
    const int port = PortFromEnv();
    std::cout << "srs_server listening on " << (bind.empty() ? "*" : bind) << ':' << port
              << '\n'
              << std::flush;
    return srs::RunServer(port, bind);
}