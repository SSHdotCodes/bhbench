cmake_minimum_required(VERSION 3.20)
project(BlackHoleSim LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GLFW_INCLUDE_DIRS}
    /opt/homebrew/include
)

link_directories(
    ${GLFW_LIBRARY_DIRS}
    /opt/homebrew/lib
)

set(SOURCES
    main.mm
    MetalRenderer.mm
    HUDOverlay.cpp
)

set(HEADERS
    ShaderTypes.h
    Camera.h
    HUDOverlay.h
    MetalRenderer.h
)

add_executable(black_hole_sim ${SOURCES} ${HEADERS})

target_link_libraries(black_hole_sim
    ${GLFW_LIBRARIES}
    ${METAL_FRAMEWORK}
    ${COCOA_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
)

# Copy shader files alongside executable
add_custom_command(TARGET black_hole_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/BlackHoleShader.metal"
    "$<TARGET_FILE_DIR:black_hole_sim>/BlackHoleShader.metal"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/ShaderTypes.h"
    "$<TARGET_FILE_DIR:black_hole_sim>/ShaderTypes.h"
)
