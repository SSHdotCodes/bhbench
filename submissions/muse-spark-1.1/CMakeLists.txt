cmake_minimum_required(VERSION 3.15)
project(BlackHoleSimulation)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(OpenGL REQUIRED)

# GLFW - try system first, then fallback to FetchContent
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "glfw3 not found via find_package, fetching...")
    include(FetchContent)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.9
    )
    FetchContent_MakeAvailable(glfw)
    set(GLFW_TARGET glfw)
else()
    set(GLFW_TARGET glfw)
    message(STATUS "Found glfw3 via system")
endif()

# GLM - header only
find_package(glm QUIET)
if(NOT glm_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 0.9.9.8
    )
    FetchContent_MakeAvailable(glm)
endif()

# GLAD only for non-Apple (Apple has gl3.h)
if(NOT APPLE)
    include(FetchContent)
    FetchContent_Declare(
        glad
        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
        GIT_TAG v0.1.36
    )
    FetchContent_MakeAvailable(glad)
    set(GLAD_TARGET glad)
else()
    set(GLAD_TARGET "")
endif()

set(SOURCES
    src/main.cpp
)

add_executable(blackhole ${SOURCES})

target_include_directories(blackhole PRIVATE src)

target_link_libraries(blackhole PRIVATE OpenGL::GL ${GLFW_TARGET})

if(NOT APPLE AND GLAD_TARGET)
    target_link_libraries(blackhole PRIVATE ${GLAD_TARGET})
endif()

if(APPLE)
    # macOS frameworks
    target_link_libraries(blackhole PRIVATE "-framework Cocoa -framework IOKit -framework CoreVideo")
endif()

# Copy shaders to build dir (optional, since we embed them but keep for reference)
add_custom_command(TARGET blackhole POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:blackhole>/shaders
)

# Set output directory
set_target_properties(blackhole PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(APPLE)
    target_compile_definitions(blackhole PRIVATE GL_SILENCE_DEPRECATION)
endif()

message(STATUS "Apple: ${APPLE}")
message(STATUS "OpenGL found: ${OPENGL_FOUND}")
