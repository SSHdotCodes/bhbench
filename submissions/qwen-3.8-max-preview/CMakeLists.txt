cmake_minimum_required(VERSION 3.16)
project(BlackHoleSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

find_path(GLEW_INCLUDE_DIR GL/glew.h PATHS /opt/homebrew/include /usr/local/include)
find_library(GLEW_LIBRARY NAMES GLEW PATHS /opt/homebrew/lib /usr/local/lib)

find_path(GLM_INCLUDE_DIR glm/glm.hpp PATHS /opt/homebrew/include /usr/local/include)

add_executable(blackhole main.cpp)

target_include_directories(blackhole PRIVATE ${GLEW_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
target_link_libraries(blackhole PRIVATE glfw OpenGL::GL ${GLEW_LIBRARY})

file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})
