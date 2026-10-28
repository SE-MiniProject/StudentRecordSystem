cmake_minimum_required(VERSION 3.20)
project(srs_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings are errors from the first commit. Adding this later means a cleanup
# task, which is more expensive than never letting the warnings in.
add_library(srs_options INTERFACE)
if(MSVC)
    target_compile_options(srs_options INTERFACE /W4 /WX)
else()
    target_compile_options(srs_options INTERFACE -Wall -Wextra -Werror -pedantic)
endif()

# Libraries the server and the tests both need: vendored headers only.
add_library(srs_deps INTERFACE)
target_include_directories(srs_deps INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
find_package(Threads REQUIRED)
target_link_libraries(srs_deps INTERFACE Threads::Threads)
if(WIN32)
    target_link_libraries(srs_deps INTERFACE ws2_32)
endif()

add_library(srs_core STATIC
    src/api/error_response.cpp
    src/api/router.cpp
    src/auth/login_route.cpp
    src/auth/password_hash.cpp
    src/auth/default_user_store.cpp
    src/validation/validation.cpp
)
target_include_directories(srs_core PUBLIC src)
target_link_libraries(srs_core PUBLIC srs_options srs_deps)

# OpenSSL provides the CSPRNG and SHA-256. The contract's hash column is
# algorithm-tagged, so Argon2id can arrive later without a schema change, but
# the first implementation needs only these two primitives.
find_package(OpenSSL REQUIRED)
target_link_libraries(srs_core PUBLIC OpenSSL::Crypto)

add_executable(srs_server src/main.cpp)
target_link_libraries(srs_server PRIVATE srs_core)

include(CTest)
if(BUILD_TESTING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()