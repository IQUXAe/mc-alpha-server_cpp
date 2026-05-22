cmake_minimum_required(VERSION 3.16)
project(alpha_server LANGUAGES CXX)

include(CheckIPOSupported)

# Rust bridge integration is mandatory and always compiled

# Optional clang-tidy static analysis (compile-time code quality checks)
option(CLANG_TIDY "Run clang-tidy during compilation" OFF)
if(CLANG_TIDY)
    find_program(CLANG_TIDY_BIN clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_BIN}")
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_BIN}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_library(LEVELDB_LIBRARY NAMES leveldb REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")
list(FILTER SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_executable(alpha_server ${SOURCES} src/main.cpp ${HEADERS})

target_link_libraries(alpha_server PRIVATE Threads::Threads ${LEVELDB_LIBRARY})
target_include_directories(alpha_server PRIVATE src)

check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
    set_property(TARGET alpha_server PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(UNIX)
    target_link_options(alpha_server PRIVATE -rdynamic)
    target_compile_options(alpha_server PRIVATE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
        -Werror=return-type
        -Werror=uninitialized

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

# Tests
option(BUILD_TESTS "Build test suite" ON)
if(BUILD_TESTS)
    enable_testing()

    # CTest will discover tests from gtest_discover_tests in test/
    add_subdirectory(test)

    # Smoke test: verify the server binary can be launched
    if(TARGET alpha_server)
        add_test(NAME server_smoke
            COMMAND sh -c "timeout 3 \"$<TARGET_FILE:alpha_server>\" || test \$? -eq 124"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        set_tests_properties(server_smoke PROPERTIES
            TIMEOUT 10
        )
    endif()
endif()

set(RUST_BRIDGE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/rust/alpha_bridge/target/release/libalpha_bridge.a")

# Custom target to compile Rust crate via Cargo automatically
add_custom_target(build_rust_bridge
    COMMAND cargo build --release
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/rust/alpha_bridge"
    BYPRODUCTS "${RUST_BRIDGE_LIB}"
    COMMENT "Compiling alpha_bridge Rust crate..."
)

add_library(alpha_rust_bridge STATIC IMPORTED GLOBAL)
set_target_properties(alpha_rust_bridge PROPERTIES
    IMPORTED_LOCATION "${RUST_BRIDGE_LIB}"
)
add_dependencies(alpha_rust_bridge build_rust_bridge)

target_compile_definitions(alpha_server PRIVATE ALPHA_ENABLE_RUST=1)
target_include_directories(alpha_server PRIVATE rust/alpha_bridge)
target_link_libraries(alpha_server PRIVATE alpha_rust_bridge gcc_s util rt dl c m)
message(STATUS "Rust bridge integration is enabled and automated.")
