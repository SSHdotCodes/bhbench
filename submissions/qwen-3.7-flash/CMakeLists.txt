cmake_minimum_required(VERSION 3.16)
project(BlackHoleSimulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(BlackHole src/main.cpp)
target_include_directories(BlackHole PRIVATE ${OPENGL_INCLUDE_DIR})
target_link_libraries(BlackHole PRIVATE glfw ${OPENGL_LIBRARIES})
