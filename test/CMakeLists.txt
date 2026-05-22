include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

file(GLOB_RECURSE TEST_SOURCES "../src/*.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*main\\.cpp$")
file(GLOB TEST_FILES "*.cpp")

add_executable(test_alpha_server ${TEST_FILES} ${TEST_SOURCES})

target_include_directories(test_alpha_server PRIVATE ../src ../rust/alpha_bridge)
target_compile_definitions(test_alpha_server PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_alpha_server PRIVATE
    GTest::gtest_main
    Threads::Threads
    ${LEVELDB_LIBRARY}
    alpha_rust_bridge gcc_s util rt dl c m
)

target_compile_options(test_alpha_server PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-deprecated
    -Werror=return-type
    -Werror=uninitialized
)

include(GoogleTest)
gtest_discover_tests(test_alpha_server)

# Register with CTest so `ctest` discovers all 199 tests
enable_testing()

# Custom target: build and run all tests
add_custom_target(check
    COMMAND test_alpha_server
    DEPENDS test_alpha_server
    COMMENT "Running all unit tests..."
    USES_TERMINAL
)
