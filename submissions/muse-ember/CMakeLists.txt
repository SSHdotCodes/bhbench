cmake_minimum_required(VERSION 3.16)
project(black_hole_cpp_muse_ember VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- Dependencies -----------------------------------------------------------
# OpenGL (on macOS this is the system OpenGL framework; on Linux Mesa/GLVND).
find_package(OpenGL REQUIRED)

# GLFW -- tries a CMake package first, then falls back to pkg-config.
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig)
    pkg_check_modules(GLFW3 QUIET glfw3)
  endif()
endif()

# GLEW -- OpenGL function loader. On macOS: `brew install glew glfw`.
# On Linux/Windows the same packages exist as glew / libglew-dev.
find_package(GLEW QUIET)

add_executable(black-hole src/main.cpp)

target_include_directories(black-hole PRIVATE src)

if(glfw3_FOUND)
  target_link_libraries(black-hole PRIVATE glfw)
elseif(GLFW3_FOUND)
  target_include_directories(black-hole PRIVATE ${GLFW3_INCLUDE_DIRS})
  target_link_libraries(black-hole PRIVATE ${GLFW3_LIBRARIES} ${GLFW3_STATIC_LIBRARIES})
else()
  find_library(GLFW_LIBRARY NAMES glfw glfw3 REQUIRED)
  find_path(GLFW_INCLUDE_DIR NAMES GLFW/glfw3.h REQUIRED)
  target_include_directories(black-hole PRIVATE ${GLFW_INCLUDE_DIR})
  target_link_libraries(black-hole PRIVATE ${GLFW_LIBRARY})
endif()

if(GLEW_FOUND)
  target_link_libraries(black-hole PRIVATE GLEW::GLEW)
else()
  find_library(GLEW_LIBRARY NAMES GLEW glew REQUIRED)
  find_path(GLEW_INCLUDE_DIR NAMES GL/glew.h REQUIRED)
  target_include_directories(black-hole PRIVATE ${GLEW_INCLUDE_DIR})
  target_link_libraries(black-hole PRIVATE ${GLEW_LIBRARY})
endif()

target_link_libraries(black-hole PRIVATE OpenGL::GL)

if(APPLE)
  # OpenGL 4.1 core on macOS requires the (deprecated but still shipped)
  # OpenGL framework and a forward-compatible context. No Metal code is
  # needed: the geodesic integration runs in the fragment shader on the GPU.
  find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
  find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  target_link_libraries(black-hole PRIVATE
    ${COCOA_FRAMEWORK} ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
  set_target_properties(black-hole PROPERTIES
    MACOSX_BUNDLE FALSE
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
endif()

target_compile_options(black-hole PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)
if(APPLE)
  target_compile_options(black-hole PRIVATE -Wno-deprecated-declarations)
endif()

message(STATUS "Black hole simulator: OpenGL + GLFW + GLEW (GPU fragment-shader ray tracing)")
