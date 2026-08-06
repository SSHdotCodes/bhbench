cmake_minimum_required(VERSION 3.20)

project(black_hole_cpp_codexlunaxhigh LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
    set(GLFW_TARGET PkgConfig::GLFW3)
else()
    find_package(glfw3 CONFIG REQUIRED)
    set(GLFW_TARGET glfw)
endif()

add_executable(black_hole
    main.cpp
)

target_link_libraries(black_hole PRIVATE
    ${GLFW_TARGET}
    OpenGL::GL
)

if(APPLE)
    target_compile_definitions(black_hole PRIVATE GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    target_compile_options(black_hole PRIVATE /W4)
else()
    target_compile_options(black_hole PRIVATE -Wall -Wextra -Wpedantic)
endif()

