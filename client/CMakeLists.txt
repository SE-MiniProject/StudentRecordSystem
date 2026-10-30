cmake_minimum_required(VERSION 3.20)
project(srs_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

add_executable(srs_client
    src/main.cpp
    src/apiclient/ApiClient.cpp
    src/apiclient/ApiClient.h
    src/login/LoginDialog.cpp
    src/login/LoginDialog.h
    src/shell/MainWindow.cpp
    src/shell/MainWindow.h
    src/students/StudentsWidget.cpp
    src/students/StudentsWidget.h
)

target_include_directories(srs_client PRIVATE src)
target_link_libraries(srs_client PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network)