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

# Build modes must be known before dependency discovery.
option(BUILD_WITH_QT "Build with Qt UI support" OFF)
option(BUILD_TESTS "Build unit tests" ON)

# Qt helpers — must be set before add_library/add_executable
if(BUILD_WITH_QT)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
endif()

# Compile options
if(MSVC)
    add_compile_options(/W4 /utf-8 /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# vcpkg-managed dependencies
find_package(nlohmann_json CONFIG REQUIRED)
if(BUILD_WITH_QT)
    find_package(spdlog CONFIG REQUIRED)
    find_package(talib CONFIG REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network)
    add_definitions(-DST_BUILD_WITH_QT)
endif()
if(BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
endif()

# Sources
add_subdirectory(src)

# Tests
if(BUILD_TESTS)
    enable_testing()
    include(GoogleTest)
    add_subdirectory(tests)
endif()
