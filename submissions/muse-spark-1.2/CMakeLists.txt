cmake_minimum_required(VERSION 3.16)
project(BlackHoleSim VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# --- Dependencies ---
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 3.3 REQUIRED)

# GLFW on Homebrew is usually found via pkg-config / cmake config at /opt/homebrew/lib/cmake
# GLEW provides GLEW::GLEW target
# glm is header-only; add Homebrew include path if needed
find_path(GLM_INCLUDE_DIR glm/glm.hpp
    PATHS /opt/homebrew/include /usr/local/include
)
if(NOT GLM_INCLUDE_DIR)
    message(WARNING "GLM not found at expected path; trying fallback")
    set(GLM_INCLUDE_DIR /opt/homebrew/include)
endif()

add_executable(black_hole
    src/main.cpp
)

target_include_directories(black_hole PRIVATE
    ${GLM_INCLUDE_DIR}
    /opt/homebrew/include
)

target_link_libraries(black_hole PRIVATE
    OpenGL::GL
    GLEW::GLEW
    glfw
)

# macOS: need to link frameworks and set rpath
if(APPLE)
    target_compile_definitions(black_hole PRIVATE GL_SILENCE_DEPRECATION)
    # Ensure Homebrew lib path is searched at runtime
    set_target_properties(black_hole PROPERTIES
        BUILD_RPATH "/opt/homebrew/lib"
        INSTALL_RPATH "/opt/homebrew/lib"
    )
endif()

# Warnings
target_compile_options(black_hole PRIVATE -Wall -Wextra -Wno-deprecated-declarations)

message(STATUS "OpenGL: ${OPENGL_LIBRARIES}")
message(STATUS "GLEW: ${GLEW_LIBRARIES}")
message(STATUS "GLFW: ${glfw3_DIR}")
message(STATUS "GLM include: ${GLM_INCLUDE_DIR}")
