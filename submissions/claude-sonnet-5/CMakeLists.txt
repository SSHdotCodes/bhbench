cmake_minimum_required(VERSION 3.16)
project(BlackHoleSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_PREFIX_PATH /opt/homebrew /opt/homebrew/opt/glfw /opt/homebrew/opt/glm)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)

add_executable(black_hole_sim
    src/main.cpp
    src/Shader.cpp
    src/Camera.cpp
    src/GridMesh.cpp
)

target_include_directories(black_hole_sim PRIVATE src)
target_link_libraries(black_hole_sim PRIVATE glfw OpenGL::GL glm::glm)

if(APPLE)
    target_compile_definitions(black_hole_sim PRIVATE GL_SILENCE_DEPRECATION)
endif()

# Bake the absolute shader directory into the binary so it can be launched
# from any working directory.
target_compile_definitions(black_hole_sim PRIVATE SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders/")
