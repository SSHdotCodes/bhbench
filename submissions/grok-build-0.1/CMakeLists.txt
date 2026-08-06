cmake_minimum_required(VERSION 3.12)
project(BlackHoleSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    add_definitions(-DGL_SILENCE_DEPRECATION)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")
endif()

find_package(glfw3 REQUIRED CONFIG)
find_package(glm REQUIRED CONFIG)

add_executable(blackhole
    src/main.cpp
)

target_include_directories(blackhole PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(blackhole PRIVATE
    glfw
    glm::glm
)

if(APPLE)
    find_library(OPENGL_LIBRARY OpenGL REQUIRED)
    target_link_libraries(blackhole PRIVATE ${OPENGL_LIBRARY})
    target_link_options(blackhole PRIVATE "-Wl,-rpath,/opt/homebrew/lib")
endif()

# Copy shaders next to binary for easy run (optional)
add_custom_command(TARGET blackhole POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:blackhole>/shaders
)

# Also allow running from source tree
file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})
