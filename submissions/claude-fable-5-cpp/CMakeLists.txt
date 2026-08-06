cmake_minimum_required(VERSION 3.16)
project(blackhole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 3.3 REQUIRED)

add_executable(blackhole src/main.cpp)

target_compile_definitions(blackhole PRIVATE
  GL_SILENCE_DEPRECATION
  SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

target_link_libraries(blackhole PRIVATE
  glfw
  "-framework OpenGL"
  "-framework Cocoa"
  "-framework IOKit"
  "-framework CoreVideo"
)
