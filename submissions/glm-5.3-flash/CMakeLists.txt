cmake_minimum_required(VERSION 3.21)
project(blackhole_cpp_ox CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 3.3 REQUIRED)

add_executable(blackhole src/main.cpp)
target_compile_definitions(blackhole PRIVATE GL_SILENCE_DEPRECATION)
target_compile_options(blackhole PRIVATE -O2 -Wall)
find_library(OPENGL_FW OpenGL REQUIRED)
target_link_libraries(blackhole PRIVATE glfw ${OPENGL_FW}
    "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
