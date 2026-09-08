cmake_minimum_required(VERSION 3.16)
project(black-hole-cpp-musemax LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(blackhole
  src/main.cpp
  src/physics.cpp
  src/shaders.cpp
  src/spacetime.cpp
  src/overlay.cpp
)

# Bake the shader directory in so the binary runs from anywhere.
target_compile_definitions(blackhole PRIVATE
  SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  GL_SILENCE_DEPRECATION=1)

# --- glm (header-only) ---
find_path(GLM_INCLUDE_DIR glm/glm.hpp
  HINTS /opt/homebrew/include /usr/local/include /usr/include)
if(NOT GLM_INCLUDE_DIR)
  message(FATAL_ERROR "glm headers not found (brew install glm)")
endif()
target_include_directories(blackhole PRIVATE ${GLM_INCLUDE_DIR})

# --- GLFW ---
find_package(glfw3 3.3 QUIET)
if(glfw3_FOUND)
  set(GLFW_TARGET glfw)
else()
  find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
    HINTS /opt/homebrew/include /usr/local/include /usr/include)
  find_library(GLFW_LIBRARY NAMES glfw glfw3
    HINTS /opt/homebrew/lib /usr/local/lib /usr/lib)
  if(NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY)
    message(FATAL_ERROR "GLFW3 not found (brew install glfw)")
  endif()
  target_include_directories(blackhole PRIVATE ${GLFW_INCLUDE_DIR})
endif()

# --- OpenGL ---
if(APPLE)
  # macOS ships OpenGL 4.1 headers with Xcode; no loader library needed.
  find_library(OPENGL_LIBRARY OpenGL REQUIRED)
  target_link_libraries(blackhole PRIVATE ${OPENGL_LIBRARY})
else()
  find_package(OpenGL REQUIRED)
  # Non-Apple needs an extension loader for core-profile entry points.
  find_path(GLEW_INCLUDE_DIR GL/glew.h)
  find_library(GLEW_LIBRARY NAMES GLEW glew32)
  if(NOT GLEW_INCLUDE_DIR OR NOT GLEW_LIBRARY)
    message(FATAL_ERROR "GLEW not found (apt install libglew-dev, etc.)")
  endif()
  target_include_directories(blackhole PRIVATE ${GLEW_INCLUDE_DIR})
  target_link_libraries(blackhole PRIVATE OpenGL::GL ${GLEW_LIBRARY})
endif()

if(glfw3_FOUND)
  target_link_libraries(blackhole PRIVATE glfw)
else()
  target_link_libraries(blackhole PRIVATE ${GLFW_LIBRARY})
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(blackhole PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
