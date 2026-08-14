cmake_minimum_required(VERSION 3.16)
project(black_hole_glm53 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(blackhole src/main.cpp)
target_link_libraries(blackhole PRIVATE glfw OpenGL::GL)

add_executable(physics_test src/physics_test.cpp)

if(APPLE)
    target_link_libraries(blackhole PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()
