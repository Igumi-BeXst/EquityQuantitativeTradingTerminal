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

# --- Third-party dependencies via FetchContent ---
include(FetchContent)

# nlohmann/json (header-only JSON library)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

# spdlog (fast C++ logging)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# cpp-httplib (C++ HTTP/HTTPS library, header-only)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.19.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(httplib)

# GoogleTest
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)
# Prevent gtest from installing itself
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# --- Qt (optional, off until installed) ---
option(BUILD_WITH_QT "Build with Qt UI support" OFF)
if(BUILD_WITH_QT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network)
    add_definitions(-DST_BUILD_WITH_QT)
endif()

# --- Source ---
add_subdirectory(src)

# --- Tests ---
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
