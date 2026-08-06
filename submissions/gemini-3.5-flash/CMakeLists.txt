cmake_minimum_required(VERSION 3.10)
project(BlackHoleSimulation CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Homebrew paths for Apple Silicon and Intel macOS
if(APPLE)
    include_directories(/opt/homebrew/include /usr/local/include)
    link_directories(/opt/homebrew/lib /usr/local/lib)
endif()

# Find OpenGL
find_package(OpenGL REQUIRED)

# Attempt to find GLFW3 via CMake configs
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    # Fallback to finding the library directly
    find_library(GLFW_LIB NAMES glfw glfw3 PATHS /opt/homebrew/lib /usr/local/lib)
    if(NOT GLFW_LIB)
        message(FATAL_ERROR "Could not find GLFW library. Install via 'brew install glfw'.")
    endif()
else()
    set(GLFW_LIB glfw)
endif()

# Sources
set(SOURCES
    main.cpp
    Shader.cpp
    Grid.cpp
    ParticleSystem.cpp
)

add_executable(black_hole_sim ${SOURCES})

target_include_directories(black_hole_sim PRIVATE ${OPENGL_INCLUDE_DIR})

if(APPLE)
    # Link GLFW, OpenGL, and required macOS frameworks
    target_link_libraries(black_hole_sim PRIVATE
        ${GLFW_LIB}
        ${OPENGL_LIBRARIES}
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
else()
    # On Linux/Windows, we typically need GLEW to load OpenGL extensions
    find_package(GLEW REQUIRED)
    target_link_libraries(black_hole_sim PRIVATE
        ${GLFW_LIB}
        ${GLEW_LIBRARIES}
        ${OPENGL_LIBRARIES}
    )
endif()
