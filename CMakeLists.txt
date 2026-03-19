cmake_minimum_required(VERSION 3.16)
project(alpha_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")

add_executable(alpha_server ${SOURCES} ${HEADERS})

target_include_directories(alpha_server PRIVATE src)
target_link_libraries(alpha_server PRIVATE ZLIB::ZLIB Threads::Threads)

if(UNIX)
    target_compile_options(alpha_server PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
