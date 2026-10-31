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
    test_logout.cpp
    test_rbac.cpp
    test_error_handler.cpp
    test_validation.cpp
    test_seed_data.cpp
    test_student_routes.cpp
    test_static_files.cpp
)
target_link_libraries(srs_tests PRIVATE srs_core GTest::gtest_main)

# The seed fixture lives in the repository, not beside the test binary, and it
# must be read by a test that runs with the build tree as its working directory.
# Baking the absolute path in keeps that test independent of CWD depth.
target_compile_definitions(
    srs_tests
    PRIVATE SRS_SEED_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../../db/seeds/demo_data.sql"
)


include(GoogleTest)
gtest_discover_tests(srs_tests)