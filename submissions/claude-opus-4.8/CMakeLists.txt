cmake_minimum_required(VERSION 3.16)
project(black_hole_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

if(NOT APPLE)
  message(WARNING "This project targets macOS (OpenGL 4.1 core + GLFW). It may need tweaks elsewhere.")
endif()

add_executable(blackhole src/main.cpp)

# --- GLFW (Homebrew) ---------------------------------------------------------
find_package(glfw3 3.3 QUIET)
if(glfw3_FOUND)
  target_link_libraries(blackhole PRIVATE glfw)
else()
  find_library(GLFW_LIB NAMES glfw glfw3 PATHS /opt/homebrew/lib /usr/local/lib)
  find_path(GLFW_INC GLFW/glfw3.h PATHS /opt/homebrew/include /usr/local/include)
  if(NOT GLFW_LIB OR NOT GLFW_INC)
    message(FATAL_ERROR "GLFW not found. Install with:  brew install glfw")
  endif()
  target_include_directories(blackhole PRIVATE ${GLFW_INC})
  target_link_libraries(blackhole PRIVATE ${GLFW_LIB})
endif()

target_include_directories(blackhole PRIVATE /opt/homebrew/include /usr/local/include)

# Tell the program where the .vert/.frag files live (absolute path = no cwd issues).
target_compile_definitions(blackhole PRIVATE
  GL_SILENCE_DEPRECATION
  SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")

# --- Apple frameworks --------------------------------------------------------
if(APPLE)
  target_link_libraries(blackhole PRIVATE
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
    "-framework CoreFoundation")
endif()
