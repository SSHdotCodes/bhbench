cmake_minimum_required(VERSION 3.16)
project(BlackHoleSim CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
add_executable(blackhole src/main.cpp)
target_include_directories(blackhole PRIVATE ${GLFW_INCLUDE_DIRS})
target_link_directories(blackhole PRIVATE ${GLFW_LIBRARY_DIRS})
target_link_libraries(blackhole PRIVATE OpenGL::GL ${GLFW_LIBRARIES})
target_compile_options(blackhole PRIVATE -O3 -Wall -Wextra)
# Copy shaders next to binary and keep source-relative lookup working
add_custom_command(TARGET blackhole POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:blackhole>/shaders)
