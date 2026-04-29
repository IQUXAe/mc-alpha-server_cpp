cmake_minimum_required(VERSION 3.16)
project(alpha_server LANGUAGES CXX)

include(CheckIPOSupported)

option(ALPHA_ENABLE_RUST "Build and link the optional Rust bridge crate" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_library(LEVELDB_LIBRARY NAMES leveldb REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)

# Prefer static zstd to avoid runtime dependency
find_library(ZSTD_STATIC_LIB NAMES libzstd.a zstd)
if(ZSTD_STATIC_LIB)
    set(ZSTD_LINK_LIB ${ZSTD_STATIC_LIB})
else()
    set(ZSTD_LINK_LIB ${ZSTD_LIBRARIES})
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")
list(FILTER SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_executable(alpha_server ${SOURCES} src/main.cpp ${HEADERS})

target_link_libraries(alpha_server PRIVATE ZLIB::ZLIB Threads::Threads ${LEVELDB_LIBRARY} ${ZSTD_LINK_LIB})
target_include_directories(alpha_server PRIVATE src ${ZSTD_INCLUDE_DIRS})

check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
    set_property(TARGET alpha_server PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(UNIX)
    target_link_options(alpha_server PRIVATE -rdynamic)
    target_compile_options(alpha_server PRIVATE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter

        $<$<CONFIG:Release>:
            -O3
            -DNDEBUG
        >

        $<$<CONFIG:Debug>:
            -O0
            -g3
            -fsanitize=address,undefined  # catch memory bugs and UB
        >
    )

    target_link_options(alpha_server PRIVATE
        $<$<CONFIG:Debug>:
            -fsanitize=address,undefined
        >
    )
endif()

if(ALPHA_ENABLE_RUST)
    set(RUST_BRIDGE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/rust/alpha_bridge/target/release/libalpha_bridge.a")
    if(NOT EXISTS "${RUST_BRIDGE_LIB}")
        message(FATAL_ERROR "Rust bridge enabled, but ${RUST_BRIDGE_LIB} was not found. Build it first with cargo.")
    endif()

    add_library(alpha_rust_bridge STATIC IMPORTED GLOBAL)
    set_target_properties(alpha_rust_bridge PROPERTIES
        IMPORTED_LOCATION "${RUST_BRIDGE_LIB}"
    )

    target_compile_definitions(alpha_server PRIVATE ALPHA_ENABLE_RUST=1)
    target_include_directories(alpha_server PRIVATE rust/alpha_bridge)
    target_link_libraries(alpha_server PRIVATE alpha_rust_bridge gcc_s util rt dl c m)
    message(STATUS "Rust bridge integration is enabled.")
endif()
