cmake_minimum_required(VERSION 3.20)
project(srs_server LANGUAGES CXX)

# C++20, not 17: libpqxx 8.x is a C++20 library (it uses std::source_location,
# std::span, concepts and std::ranges in its public headers). Building against
# it as C++17 fails inside pqxx's own headers before any project code is read.
# Ubuntu's libpqxx-dev is 7.x and would tolerate C++17, but pinning to the
# older standard would make the build depend on the distro rather than on what
# the code requires.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Headers are vendored under server/third_party (cpp-httplib 0.18.3,
# nlohmann/json 3.11.3) and exposed through srs_deps below. They are
# deliberately not fetched at configure time: the build must work offline and
# from a fresh clone without reaching the network.
include(FetchContent)  # kept for future dependencies; not used yet

# PostgreSQL C++ client library, pinned to the 8.x API the stores are written
# against. This is fetched rather than taken from the distro on purpose: Ubuntu
# 24.04 and 26.04 both ship libpqxx 7.10, whose exec() has no (query, params)
# overload, so the pg_*_store sources do not compile there. Taking the distro
# package also made the build depend on the host's exact version, and a partial
# mismatch (7.10 headers against an 8.x shared library) surfaced as
# "double free detected in tcache 2" during static initialisation, before main()
# ran. Fetching one release tag keeps every environment on the same code.
FetchContent_Declare(
    libpqxx
    GIT_REPOSITORY https://github.com/jtv/libpqxx.git
    GIT_TAG        8.0.2
    GIT_SHALLOW    TRUE
)
# libpqxx's own extras are not part of what this project consumes. Its switches
# are BUILD_TEST/BUILD_TOOLS; note that BUILD_TESTING is deliberately left
# alone, because this project reads that same variable to decide whether to
# build server/tests, and forcing it off here would silently skip the suite.
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(PQXX_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libpqxx)

# Strict compiler warnings.
# Warnings are treated as errors to maintain code quality.
add_library(srs_options INTERFACE)

if(MSVC)
    target_compile_options(srs_options INTERFACE /W4 /WX)
else()
    target_compile_options(
        srs_options
        INTERFACE
        -Wall
        -Wextra
        -Werror
        -pedantic
    )
endif()

# Shared dependencies required by the server and tests.
add_library(srs_deps INTERFACE)

target_include_directories(
    srs_deps
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

find_package(Threads REQUIRED)

target_link_libraries(
    srs_deps
    INTERFACE
    Threads::Threads
)

if(WIN32)
    target_link_libraries(srs_deps INTERFACE ws2_32)
endif()

# OpenSSL provides cryptographic primitives for authentication.
find_package(OpenSSL REQUIRED)

# Core server library.
add_library(
    srs_core
    STATIC
    src/api/error_response.cpp
    src/api/router.cpp
    src/auth/bearer_token.cpp
    src/auth/login_route.cpp
    src/auth/logout_route.cpp
    src/auth/password_hash.cpp
    src/auth/rbac.cpp
    src/auth/default_user_store.cpp
    src/auth/default_session_store.cpp
    src/auth/pg_user_store.cpp
    src/auth/pg_session_store.cpp
    src/validation/validation.cpp
)

target_include_directories(
    srs_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Expose the required dependencies to the server and consumers of srs_core.
target_link_libraries(
    srs_core
    PUBLIC
    srs_options
    srs_deps
    OpenSSL::Crypto
    pqxx
)

# Main server executable.
add_executable(srs_server src/main.cpp)

target_link_libraries(
    srs_server
    PRIVATE
    srs_core
)

# Optional CTest integration.
include(CTest)

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
