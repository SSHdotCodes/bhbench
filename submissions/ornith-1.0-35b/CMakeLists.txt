cmake_minimum_required(VERSION 3.15)
project(black-hole-sim VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find GLFW3 from Homebrew (macOS) or system package manager (Linux)
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    # Try Homebrew paths for macOS
    if(APPLE)
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
        find_package(glfw3 REQUIRED)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW REQUIRED glfw3)
    endif()
endif()

# Find GLM math library (usually bundled with GLFW from Homebrew, or separate)
find_path(GLM_INCLUDE_DIR glm/glm.hpp
    PATHS /opt/homebrew/include /usr/local/include ${CMAKE_SOURCE_DIR}/include)
if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR "GLM not found. Install via: brew install glm")
endif()

# Find OpenGL
find_package(OpenGL REQUIRED)

# Collect shader files for copying to build directory
file(GLOB SHADER_FILES ${CMAKE_SOURCE_DIR}/shaders/*.vert ${CMAKE_SOURCE_DIR}/shaders/*.frag)

# Main executable
add_executable(black-hole-sim main.cpp ${SHADER_FILES})

target_include_directories(black-hole-sim PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${GLM_INCLUDE_DIR}
)

# Silence macOS OpenGL deprecation warnings (OpenGL is deprecated on macOS 10.14+)
if(APPLE)
    target_compile_definitions(black-hole-sim PRIVATE GL_SILENCE_DEPRECATION)
endif()

if(glfw3_FOUND AND TARGET glfw)
    target_link_libraries(black-hole-sim PRIVATE glfw OpenGL::GL)
else()
    target_include_directories(black-hole-sim PRIVATE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(black-hole-sim PRIVATE ${GLFW_LIBRARIES} OpenGL::GL)
endif()

# Copy shaders to build directory so the executable can find them
add_custom_command(TARGET black-hole-sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:black-hole-sim>/shaders
)
