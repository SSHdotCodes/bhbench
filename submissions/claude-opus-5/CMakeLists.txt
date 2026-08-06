cmake_minimum_required(VERSION 3.20)
project(blackhole VERSION 1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-O3")

list(APPEND CMAKE_PREFIX_PATH /opt/homebrew /usr/local)
find_package(glfw3 REQUIRED)

add_executable(blackhole
    src/main.cpp
    src/app.cpp
    src/glutil.cpp
    src/spectrum.cpp
    src/geometry.cpp
    src/verify.cpp
    src/text.mm
)

target_include_directories(blackhole PRIVATE src /opt/homebrew/include)
target_compile_definitions(blackhole PRIVATE
    GL_SILENCE_DEPRECATION
    GLFW_INCLUDE_NONE
    SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
target_link_libraries(blackhole PRIVATE
    glfw
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
    "-framework CoreText"
    "-framework CoreGraphics"
    "-framework Foundation"
)
