cmake_minimum_required(VERSION 3.16)
project(black_hole_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

add_executable(black_hole_sim
    src/main.cpp
    src/shader.cpp
    src/camera.cpp
    src/mesh.cpp
)

target_include_directories(black_hole_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(black_hole_sim PRIVATE
    OpenGL::GL
    glfw
    glm::glm
)

# macOS: OpenGL is a framework; ensure forward-compatible core profile works
if(APPLE)
    target_compile_definitions(black_hole_sim PRIVATE GL_SILENCE_DEPRECATION)
endif()

# Copy shaders next to the binary for easy loading
add_custom_command(TARGET black_hole_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:black_hole_sim>/shaders
    COMMENT "Copying shaders to build directory"
)

# Also keep a copy at project root runtime convenience
add_custom_command(TARGET black_hole_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/shaders
)
