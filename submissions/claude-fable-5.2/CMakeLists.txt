cmake_minimum_required(VERSION 3.16)
project(KerrBlackHole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

# ---- shaders are embedded in the executable ---------------------------------
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
file(GLOB SHADER_FILES ${SHADER_DIR}/*.glsl ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.frag)
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
set(EMBEDDED_HEADER ${GENERATED_DIR}/shaders_embedded.hpp)
file(MAKE_DIRECTORY ${GENERATED_DIR})
add_custom_command(
    OUTPUT ${EMBEDDED_HEADER}
    COMMAND ${CMAKE_COMMAND} -DSHADER_DIR=${SHADER_DIR} -DOUT=${EMBEDDED_HEADER}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_shaders.cmake
    DEPENDS ${SHADER_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed_shaders.cmake
    COMMENT "Embedding shaders"
    VERBATIM)

# ---- the simulation ----------------------------------------------------------
add_executable(blackhole
    src/main.cpp
    src/hud_text.cpp
    ${EMBEDDED_HEADER})
target_include_directories(blackhole PRIVATE src ${GENERATED_DIR})
target_link_libraries(blackhole PRIVATE glfw OpenGL::GL ZLIB::ZLIB)
target_compile_options(blackhole PRIVATE -Wall -Wextra -Wno-unused-parameter)
if(APPLE)
    target_link_libraries(blackhole PRIVATE
        "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation")
endif()

# ---- physics verification (no GPU needed) ------------------------------------
# Compiles shaders/kerr_geodesic.glsl as C++ and checks it against analytic GR.
add_executable(verify tests/verify.cpp)
# No fused multiply-add: keep the float32 run faithful to plain IEEE arithmetic.
target_compile_options(verify PRIVATE -ffp-contract=off -Wall -Wno-unused-function)

enable_testing()
add_test(NAME physics COMMAND verify)
