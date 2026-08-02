cmake_minimum_required(VERSION 3.21)
project(StockTerminal
    VERSION 0.1.0
    DESCRIPTION "Comprehensive Stock Trading Workstation"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt helpers — must be set before add_library/add_executable
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Compile options
if(MSVC)
    add_compile_options(/W4 /utf-8 /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# vcpkg-managed dependencies
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# TA-Lib technical analysis library
find_package(talib CONFIG REQUIRED)

# Qt (optional, from separate install)
option(BUILD_WITH_QT "Build with Qt UI support" OFF)
if(BUILD_WITH_QT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network)
    add_definitions(-DST_BUILD_WITH_QT)
endif()

# Sources
add_subdirectory(src)

# Tests
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    include(GoogleTest)
    add_subdirectory(tests)
endif()
