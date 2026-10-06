cmake_minimum_required(VERSION 3.16)
project(blackhole CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(GLEW REQUIRED)

add_executable(blackhole src/main.cpp)
target_include_directories(blackhole PRIVATE ${OPENGL_INCLUDE_DIRS})
target_link_libraries(blackhole PRIVATE OpenGL::GL glfw GLEW::GLEW)
