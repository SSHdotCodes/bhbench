cmake_minimum_required(VERSION 3.15)
project(BlackHoleSimulation CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
)

set(IMGUI_SRCS
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_metal.mm
    external/imgui/backends/imgui_impl_osx.mm
)

# Custom command to compile Metal shader
set(METAL_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/BlackHoleRaymarch.metal)
set(METAL_AIR ${CMAKE_CURRENT_BINARY_DIR}/BlackHoleRaymarch.air)
set(METAL_LIB ${CMAKE_CURRENT_BINARY_DIR}/BlackHoleRaymarch.metallib)

add_custom_command(
    OUTPUT ${METAL_LIB}
    COMMAND xcrun metal -c ${METAL_SHADER} -I${CMAKE_CURRENT_SOURCE_DIR}/src -o ${METAL_AIR}
    COMMAND xcrun metallib ${METAL_AIR} -o ${METAL_LIB}
    DEPENDS ${METAL_SHADER} ${CMAKE_CURRENT_SOURCE_DIR}/src/ShaderTypes.h
    COMMENT "Compiling Metal Compute Shaders..."
)

add_executable(BlackHoleSim
    src/main.mm
    ${IMGUI_SRCS}
    ${METAL_LIB}
)

target_link_libraries(BlackHoleSim
    "-framework Metal"
    "-framework MetalKit"
    "-framework Cocoa"
    "-framework QuartzCore"
    "-framework IOKit"
    "-framework GameController"
)

# Copy metallib adjacent to binary for runtime loading
add_custom_command(TARGET BlackHoleSim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${METAL_LIB}
    $<TARGET_FILE_DIR:BlackHoleSim>/BlackHoleRaymarch.metallib
)
