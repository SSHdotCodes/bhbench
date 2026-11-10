cmake_minimum_required(VERSION 3.20)
project(black_hole_cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(black-hole
    src/main.cpp
    src/embedding.cpp
    src/overlay.cpp
)
target_link_libraries(black-hole PRIVATE OpenGL::GL glfw)
target_compile_definitions(black-hole PRIVATE
    BLACK_HOLE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
    GL_SILENCE_DEPRECATION
)
target_compile_options(black-hole PRIVATE -Wall -Wextra -Wpedantic)

add_custom_target(copy-shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMENT "Copying GLSL shaders"
)
add_dependencies(black-hole copy-shaders)

include(CTest)
if(BUILD_TESTING)
    add_executable(physics-tests tests/physics_tests.cpp)
    target_compile_options(physics-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME schwarzschild-physics COMMAND physics-tests)
endif()

add_custom_target(run
    COMMAND black-hole
    DEPENDS black-hole
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Opening the realtime black-hole window"
)
