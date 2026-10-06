cmake_minimum_required(VERSION 3.18)
project(black_hole_cpp_sol6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(black-hole src/main.cpp)
target_link_libraries(black-hole PRIVATE glfw OpenGL::GL)
target_compile_definitions(black-hole PRIVATE BH_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
if(APPLE)
  target_compile_definitions(black-hole PRIVATE GL_SILENCE_DEPRECATION)
  add_custom_command(TARGET black-hole POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/BlackHole.app/Contents/MacOS
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:black-hole>
      ${CMAKE_CURRENT_SOURCE_DIR}/BlackHole.app/Contents/MacOS/black-hole)
endif()

add_custom_target(run
  COMMAND $<TARGET_FILE:black-hole>
  DEPENDS black-hole
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  USES_TERMINAL)
