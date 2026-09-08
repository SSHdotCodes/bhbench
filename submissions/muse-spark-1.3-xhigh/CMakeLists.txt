cmake_minimum_required(VERSION 3.16)
project(black_hole_cpp_muse_1_3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies -----------------------------------------------------------
# GLFW (window + OpenGL context). Installed via Homebrew on macOS:
#   brew install glfw cmake
find_package(glfw3 3.3 REQUIRED)

# --- Targets ----------------------------------------------------------------
add_executable(blackhole
  src/main.cpp
)

target_include_directories(blackhole PRIVATE src)

target_link_libraries(blackhole PRIVATE glfw)

# OpenGL: macOS ships the framework; Linux uses libGL.
if(APPLE)
  target_link_libraries(blackhole PRIVATE "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(blackhole PRIVATE OpenGL::GL)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(blackhole PRIVATE -Wall -Wextra -O2)
endif()
if(APPLE)
  target_compile_definitions(blackhole PRIVATE GL_SILENCE_DEPRECATION=1)
endif()

# Copy shaders next to the binary so the exe finds them regardless of CWD.
add_custom_command(TARGET blackhole POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:blackhole>/shaders)

install(TARGETS blackhole RUNTIME DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION bin/shaders)
