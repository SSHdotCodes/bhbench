cmake_minimum_required(VERSION 3.16)
project(schwarzschild LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(glfw3 3.3 REQUIRED)

add_executable(blackhole src/main.cpp)
target_include_directories(blackhole PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(blackhole PRIVATE "SHADER_DIR=\"${CMAKE_SOURCE_DIR}/shaders\"")
target_compile_options(blackhole PRIVATE -O3 -Wall -Wextra -Wno-deprecated-declarations)
target_compile_definitions(blackhole PRIVATE GL_SILENCE_DEPRECATION)
target_link_libraries(blackhole PRIVATE glfw "-framework OpenGL")

add_executable(bh_test src/test_physics.cpp)
target_include_directories(bh_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(bh_test PRIVATE -O2 -Wall -Wextra)
