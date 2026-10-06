cmake_minimum_required(VERSION 3.16)
project(BlackHoleSimulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")

find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_library(OPENGL_LIBRARY OpenGL)

add_executable(blackhole_sim
    src/main.cpp
    src/renderer.cpp
    src/raytracer.cpp
    src/spacetime.cpp
    src/accretion.cpp
    src/starfield.cpp
)

target_include_directories(blackhole_sim PRIVATE src/)
target_link_libraries(blackhole_sim PRIVATE glfw GLEW::GLEW ${OPENGL_LIBRARY})

target_compile_options(blackhole_sim PRIVATE -O2 -Wall -Wextra)