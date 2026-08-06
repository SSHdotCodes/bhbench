cmake_minimum_required(VERSION 3.16)
project(BlackHoleSim VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

set(SOURCES
    src/main.cpp
    src/shader.cpp
    src/grid_renderer.cpp
    src/camera.cpp
)

add_executable(black-hole-sim ${SOURCES})

target_include_directories(black-hole-sim PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(black-hole-sim PRIVATE
    OpenGL::GL
    glfw
    glm::glm
)

if(APPLE)
    target_compile_definitions(black-hole-sim PRIVATE GL_SILENCE_DEPRECATION)
endif()

# Copy shaders next to executable
add_custom_command(TARGET black-hole-sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:black-hole-sim>/shaders
)