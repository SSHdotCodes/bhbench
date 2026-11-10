cmake_minimum_required(VERSION 3.16)
project(black_hole_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

if(APPLE)
  list(APPEND CMAKE_PREFIX_PATH /opt/homebrew /usr/local)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(GLEW QUIET)

add_executable(blackhole
  src/main.cpp
)

target_include_directories(blackhole PRIVATE src)

if(GLEW_FOUND)
  target_link_libraries(blackhole PRIVATE OpenGL::GL glfw GLEW::GLEW)
else()
  # Fall back to Homebrew's glew directly
  find_library(GLEW_LIB GLEW PATHS /opt/homebrew/lib /usr/local/lib REQUIRED)
  find_path(GLEW_INC GL/glew.h PATHS /opt/homebrew/include /usr/local/include REQUIRED)
  target_include_directories(blackhole PRIVATE ${GLEW_INC})
  target_link_libraries(blackhole PRIVATE OpenGL::GL glfw ${GLEW_LIB})
endif()

if(APPLE)
  target_compile_definitions(blackhole PRIVATE GL_SILENCE_DEPRECATION=1)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
