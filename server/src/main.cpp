#include <cstdlib>
#include <iostream>
#include <string>

#include "api/router.h"

namespace {

// PORT is the convention on most PaaS and container runtimes. A malformed or
// out-of-range value falls back to the default rather than silently binding 0.
int PortFromEnv() {
    const char* raw = std::getenv("PORT");
    if (raw == nullptr) {
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

}  // namespace

int main() {
    const int port = PortFromEnv();
    std::cout << "srs_server listening on port " << port << '\n' << std::flush;
    return srs::RunServer(port);
}