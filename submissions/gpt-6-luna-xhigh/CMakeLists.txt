cmake_minimum_required(VERSION 3.20)
project(black_hole_cpp_luna6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(glm CONFIG REQUIRED)

if(APPLE)
  add_executable(black_hole MACOSX_BUNDLE main.cpp)
  set_target_properties(black_hole PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.codex.black-hole-cpp-luna6"
    MACOSX_BUNDLE_BUNDLE_NAME "Schwarzschild Simulation"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
  )
else()
  add_executable(black_hole main.cpp)
endif()
target_link_libraries(black_hole PRIVATE OpenGL::GL GLEW::GLEW glfw glm::glm)

if(APPLE)
  target_compile_definitions(black_hole PRIVATE GL_SILENCE_DEPRECATION)
endif()
