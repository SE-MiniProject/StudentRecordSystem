cmake_minimum_required(VERSION 3.20)
project(srs_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch external dependencies.
include(FetchContent)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)

FetchContent_MakeAvailable(httplib)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

FetchContent_MakeAvailable(json)

# PostgreSQL C++ client library.
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)

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
    src/auth/login_route.cpp
    src/auth/password_hash.cpp
    src/auth/default_user_store.cpp
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
    httplib::httplib
    nlohmann_json::nlohmann_json
    PkgConfig::PQXX
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
