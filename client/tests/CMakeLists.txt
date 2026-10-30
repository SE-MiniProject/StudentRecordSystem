# One executable per test file. Each Qt Test source ends with QTEST_MAIN, which
# emits its own main(); linking several into one binary is a duplicate-symbol
# error. Separate binaries also give ctest one line per suite, which is what
# you want when CI fails.
set(SRS_CLIENT_TESTS
    test_session
    test_apiclient
    test_logindialog
    test_mainwindow
)

foreach(test_name IN LISTS SRS_CLIENT_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE srs_client_core Qt6::Test)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    if(MSVC)
        target_compile_options(${test_name} PRIVATE /W4 /WX)
    else()
        target_compile_options(${test_name} PRIVATE -Wall -Wextra -Werror -pedantic)
    endif()

    add_test(NAME client_${test_name} COMMAND ${test_name})

    # Qt widgets need a platform plugin; the offscreen one works with no
    # display, which is what a CI runner has.
    set_tests_properties(client_${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endforeach()