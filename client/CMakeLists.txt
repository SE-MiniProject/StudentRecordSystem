cmake_minimum_required(VERSION 3.20)
project(srs_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Tests are opt-in through the standard CTest switch so a plain configure stays
# dependency-free (no Qt Test needed for a production build).
include(CTest)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# Everything except main() lives in a static library so the tests link the same
# objects the executable does, instead of recompiling the sources.
add_library(srs_client_core STATIC
    src/apiclient/ApiClient.cpp
    src/apiclient/ApiClient.h
    src/login/LoginDialog.cpp
    src/login/LoginDialog.h
    src/session/Session.cpp
    src/session/Session.h
    src/shell/MainWindow.cpp
    src/shell/MainWindow.h
    src/students/StudentsWidget.cpp
    src/students/StudentsWidget.h
)

target_include_directories(srs_client_core PUBLIC src)
target_link_libraries(srs_client_core PUBLIC Qt6::Widgets Qt6::Network)

if(MSVC)
    target_compile_options(srs_client_core PRIVATE /W4 /WX)
else()
    target_compile_options(srs_client_core PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_executable(srs_client src/main.cpp)
target_link_libraries(srs_client PRIVATE srs_client_core)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()