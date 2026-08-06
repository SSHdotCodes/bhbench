cmake_minimum_required(VERSION 3.16)
project(BlackHoleSim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Find Homebrew installed packages on macOS arm64 / x86_64
list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")

find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm REQUIRED)

include_directories(
    ${OPENGL_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(BlackHoleSim
    src/main.cpp
)

# Copy shaders directory to build directory
add_custom_command(
    TARGET BlackHoleSim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:BlackHoleSim>/shaders
)

target_link_libraries(BlackHoleSim
    PRIVATE
    OpenGL::GL
    glfw
    GLEW::GLEW
)

# Apple specific frameworks
if(APPLE)
    target_link_libraries(BlackHoleSim PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()
