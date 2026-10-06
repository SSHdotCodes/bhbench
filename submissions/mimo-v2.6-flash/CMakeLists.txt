cmake_minimum_required(VERSION 3.20)
project(blackhole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 3.3 REQUIRED)
find_package(ZLIB QUIET)

set(BH_SOURCES
    src/main.cpp
    src/shader.cpp
    src/scene.cpp
    src/physics.cpp
    src/selftest.cpp
    src/image_io.cpp
)

add_executable(blackhole ${BH_SOURCES})
target_include_directories(blackhole PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Directory the runtime uses to load GLSL sources (overridable with --shaders).
target_compile_definitions(blackhole PRIVATE
    BH_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    BH_USE_ZLIB=$<BOOL:${ZLIB_FOUND}>
    GL_SILENCE_DEPRECATION
)

target_link_libraries(blackhole PRIVATE glfw)

if(APPLE)
  target_link_libraries(blackhole PRIVATE "-framework OpenGL")
else()
  find_package(OpenGL REQUIRED)
  find_package(GLEW REQUIRED)
  target_link_libraries(blackhole PRIVATE OpenGL::GL GLEW::GLEW)
endif()

if(ZLIB_FOUND)
  target_link_libraries(blackhole PRIVATE ZLIB::ZLIB)
endif()

if(NOT MSVC)
  target_compile_options(blackhole PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

message(STATUS "ZLIB PNG support: ${ZLIB_FOUND}")
