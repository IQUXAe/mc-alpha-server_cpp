cmake_minimum_required(VERSION 3.16)
project(alpha_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_library(LEVELDB_LIBRARY NAMES leveldb REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")
list(FILTER SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_executable(alpha_server ${SOURCES} src/main.cpp ${HEADERS})

target_include_directories(alpha_server PRIVATE src)
target_link_libraries(alpha_server PRIVATE ZLIB::ZLIB Threads::Threads ${LEVELDB_LIBRARY})

if(UNIX)
    target_compile_options(alpha_server PRIVATE
        -Wall -Wextra -Wno-unused-parameter

        $<$<CONFIG:Release>:
            -O3                  # full optimization
            -march=native        # use all CPU instructions available on this machine
            -mtune=native        # tune scheduling for this CPU
            -ffast-math          # aggressive float opts (safe for game logic, no NaN/Inf reliance)
            -funroll-loops       # unroll small loops (chunk iteration, nibble arrays)
            -fomit-frame-pointer # free up one register in hot paths
            -DNDEBUG             # disable asserts
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
