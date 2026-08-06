cmake_minimum_required(VERSION 3.14)
project(black_hole_glm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS: use the system OpenGL framework (provides GL 3.3+ core, no GLEW/GLAD needed)
find_library(OPENGL_LIBRARY OpenGL REQUIRED)
find_library(COCOA_LIBRARY Cocoa REQUIRED)
find_library(IOKIT_LIBRARY IOKit REQUIRED)

find_package(glfw3 REQUIRED)

# GLM is header-only
find_path(GLM_INCLUDE_DIR glm/glm.hpp
    HINTS /opt/homebrew/include /usr/local/include)
if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR "GLM not found. Install with: brew install glm")
endif()

add_executable(black_hole
    src/main.cpp
)

target_include_directories(black_hole PRIVATE
    ${GLM_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(black_hole PRIVATE
    SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

target_link_libraries(black_hole PRIVATE
    glfw
    ${OPENGL_LIBRARY}
    ${COCOA_LIBRARY}
    ${IOKIT_LIBRARY}
)

# macOS specifics
if(APPLE)
    target_compile_definitions(black_hole PRIVATE GL_SILENCE_DEPRECATION)
endif()
