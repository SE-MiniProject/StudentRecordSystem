include(FetchContent)

# GoogleTest is fetched rather than vendored: unlike the two headers the server
# needs, it is a full source tree and vendoring it would dominate the repository.
# The pin is a release tag so a rebuild gets the same code.
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(srs_tests
    test_health.cpp
    test_login.cpp
    test_error_handler.cpp
)
target_link_libraries(srs_tests PRIVATE srs_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(srs_tests)