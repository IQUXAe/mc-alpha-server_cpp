# Builds tests using Google Test
# To run: ctest --test-dir build  (or ./build/test_alpha_server)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Reuse all server sources relative to parent dir (excluding main.cpp)
file(GLOB_RECURSE TEST_SOURCES "../src/*.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB TEST_FILES "*.cpp")

add_executable(test_alpha_server ${TEST_FILES} ${TEST_SOURCES})

target_include_directories(test_alpha_server PRIVATE ../src ${ZSTD_INCLUDE_DIRS})
target_link_libraries(test_alpha_server PRIVATE
    GTest::gtest_main
    ZLIB::ZLIB
    Threads::Threads
    ${LEVELDB_LIBRARY}
    ${ZSTD_LINK_LIB}
)

target_compile_options(test_alpha_server PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-deprecated
)

include(GoogleTest)
gtest_discover_tests(test_alpha_server)
