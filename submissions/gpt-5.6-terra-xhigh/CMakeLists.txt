cmake_minimum_required(VERSION 3.20)
project(black_hole_cpp_codexterraxhigh LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG QUIET)

# The project remains buildable on a fresh machine: if GLFW is not supplied by
# the system package manager, CMake obtains this small windowing dependency.
if(NOT TARGET glfw)
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)
endif()

add_executable(black_hole src/main.cpp)
target_link_libraries(black_hole PRIVATE glfw OpenGL::GL)

if(APPLE)
  target_compile_definitions(black_hole PRIVATE GL_SILENCE_DEPRECATION)
endif()

target_compile_options(black_hole PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>)
