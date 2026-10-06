cmake_minimum_required(VERSION 3.16)
project(blackhole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(blackhole src/main.cpp)
target_include_directories(blackhole PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(blackhole PRIVATE -Wall -Wextra)

# --- GLFW ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GLFW QUIET glfw3)
endif()
if(GLFW_FOUND)
  target_include_directories(blackhole PRIVATE ${GLFW_INCLUDE_DIRS})
  target_link_directories(blackhole PRIVATE ${GLFW_LIBRARY_DIRS})
  target_link_libraries(blackhole PRIVATE ${GLFW_LIBRARIES})
else()
  find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
    HINTS /opt/homebrew/include /usr/local/include)
  find_library(GLFW_LIBRARY NAMES glfw glfw3
    HINTS /opt/homebrew/lib /usr/local/lib)
  if(NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY)
    message(FATAL_ERROR "GLFW not found. Install with: brew install glfw")
  endif()
  target_include_directories(blackhole PRIVATE ${GLFW_INCLUDE_DIR})
  target_link_libraries(blackhole PRIVATE ${GLFW_LIBRARY})
endif()

# --- OpenGL (on macOS the OpenGL pipeline is backed by Metal) ---
if(APPLE)
  target_compile_definitions(blackhole PRIVATE GL_SILENCE_DEPRECATION)
  target_link_libraries(blackhole PRIVATE
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo")
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(blackhole PRIVATE OpenGL::GL)
endif()
