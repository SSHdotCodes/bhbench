cmake_minimum_required(VERSION 3.15)
project(black_hole_cpp_hy3 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 REQUIRED)

# GLM header-only
find_path(GLM_INCLUDE_DIR glm/glm.hpp)
if(GLM_INCLUDE_DIR)
  include_directories(${GLM_INCLUDE_DIR})
endif()

# On macOS the OpenGL framework is provided by the system.
if(APPLE)
  find_library(OPENGL_LIB OpenGL)
else()
  find_package(OpenGL REQUIRED)
  set(OPENGL_LIB OpenGL::GL)
endif()

add_executable(blackhole
  src/main.cpp
  src/blackhole.cpp
  src/spacetime_grid.cpp
)

if(APPLE)
  target_link_libraries(blackhole PRIVATE glfw ${OPENGL_LIB})
else()
  target_link_libraries(blackhole PRIVATE glfw ${OPENGL_LIB})
endif()

target_compile_definitions(blackhole PRIVATE GLM_ENABLE_EXPERIMENTAL GL_SILENCE_DEPRECATION)
target_compile_options(blackhole PRIVATE -O3 -ffast-math)
