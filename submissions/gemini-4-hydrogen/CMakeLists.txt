cmake_minimum_required(VERSION 3.20)
project(KerrBlackHoleSim LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compile Metal shaders into default.metallib alongside the executable
set(METAL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/BlackHole.metal")
set(METAL_HDR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/ShaderTypes.h")
set(METAL_AIR "${CMAKE_CURRENT_BINARY_DIR}/BlackHole.air")
set(METAL_LIB "${CMAKE_CURRENT_BINARY_DIR}/default.metallib")

add_custom_command(
    OUTPUT "${METAL_AIR}"
    COMMAND xcrun -sdk macosx metal -O3 -c "${METAL_SRC}" -I "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders" -o "${METAL_AIR}"
    DEPENDS "${METAL_SRC}" "${METAL_HDR}"
    COMMENT "Compiling Metal shader BlackHole.metal"
    VERBATIM
)

add_custom_command(
    OUTPUT "${METAL_LIB}"
    COMMAND xcrun -sdk macosx metallib "${METAL_AIR}" -o "${METAL_LIB}"
    DEPENDS "${METAL_AIR}"
    COMMENT "Linking Metal library default.metallib"
    VERBATIM
)

add_custom_target(black_hole_shaders ALL DEPENDS "${METAL_LIB}")

add_executable(black_hole_sim
    src/main.cpp
    src/platform/MetalApp.mm
)

add_dependencies(black_hole_sim black_hole_shaders)

target_include_directories(black_hole_sim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_options(black_hole_sim PRIVATE
    -Wall -Wextra
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
)

target_link_libraries(black_hole_sim PRIVATE
    "-framework Cocoa"
    "-framework Metal"
    "-framework MetalKit"
    "-framework MetalPerformanceShaders"
    "-framework QuartzCore"
    "-framework ImageIO"
    "-framework UniformTypeIdentifiers"
)

enable_testing()
add_executable(test_physics tests/test_physics.cpp)
target_compile_options(test_physics PRIVATE -Wall -Wextra -O2)
add_test(NAME GeneralRelativityPhysicsTests COMMAND test_physics)
