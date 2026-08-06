cmake_minimum_required(VERSION 3.20)
project(black_hole_simulation CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

# Find GLEW
find_path(GLEW_INCLUDE_DIR GL/glew.h
    PATHS /opt/homebrew/include /usr/local/include /usr/include
)
find_library(GLEW_LIBRARY GLEW
    PATHS /opt/homebrew/lib /usr/local/lib /usr/lib
)

if(APPLE)
    find_library(METAL_FRAMEWORK Metal)
    find_library(FOUNDATION_FRAMEWORK Foundation)
endif()

file(GLOB SOURCES src/*.cpp)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Simulation.cpp
    src/BlackHolePhysics.cpp
    src/Renderer.cpp
    src/SpacetimeGrid.cpp
    src/AccretionDisk.cpp
    src/GeodesicTracer.cpp
    src/ShaderProgram.cpp
    src/Window.cpp
)

if(APPLE)
    target_sources(${PROJECT_NAME} PRIVATE src/MPSInteroperability.mm)
endif()

target_link_libraries(${PROJECT_NAME}
    OpenGL::GL
    glfw
    ${GLEW_LIBRARY}
    ${OPENGL_LIBRARIES}
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME}
        "-framework Metal"
        "-framework Foundation"
        "-framework MetalKit"
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_MPS)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    ${GLEW_INCLUDE_DIR}
    /opt/homebrew/include
    /usr/local/include
)

# Copy shaders to build directory
file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})
