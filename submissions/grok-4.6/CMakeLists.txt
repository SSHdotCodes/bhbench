cmake_minimum_required(VERSION 3.16)
project(black_hole_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(black_hole
    src/main.cpp
)

target_include_directories(black_hole PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    /opt/homebrew/include
    /usr/local/include
)

find_package(glfw3 REQUIRED)
target_link_libraries(black_hole PRIVATE glfw)

if(APPLE)
    target_link_libraries(black_hole PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
    target_compile_definitions(black_hole PRIVATE GL_SILENCE_DEPRECATION)
endif()

target_compile_definitions(black_hole PRIVATE
    SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(black_hole PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)
endif()

message(STATUS "Black hole simulation: ${CMAKE_BUILD_TYPE}")
message(STATUS "Shaders: ${CMAKE_SOURCE_DIR}/shaders")
