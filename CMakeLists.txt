cmake_minimum_required(VERSION 3.16)
project(alpha_server LANGUAGES CXX)

include(CheckIPOSupported)

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
