cmake_minimum_required(VERSION 3.16)
project(black_hole_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GLFW_INCLUDE_DIRS}
    /opt/homebrew/include
)
link_directories(${GLFW_LIBRARY_DIRS} /opt/homebrew/lib)

add_executable(black_hole_sim src/main.cpp)

target_link_libraries(black_hole_sim
    ${GLFW_LIBRARIES}
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
)
