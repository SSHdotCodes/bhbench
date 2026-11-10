cmake_minimum_required(VERSION 3.20)
project(BlackHole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(glfw3 3.3 REQUIRED)
find_package(GLEW REQUIRED)

# ---------------------------------------------------------------------------
# Embed GLSL sources into a generated header so the binary is self-contained.
# ---------------------------------------------------------------------------
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)
file(GLOB SHADER_FILES ${SHADER_DIR}/*.frag ${SHADER_DIR}/*.vert)
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

set(EMBED_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.cmake)
add_custom_command(
  OUTPUT ${GEN_DIR}/shaders_embedded.h
  COMMAND ${CMAKE_COMMAND}
          -DSHADER_DIR=${SHADER_DIR}
          -DOUT=${GEN_DIR}/shaders_embedded.h
          -P ${EMBED_SCRIPT}
  DEPENDS ${SHADER_FILES} ${EMBED_SCRIPT}
  COMMENT "Embedding GLSL shaders"
  VERBATIM)

add_custom_target(shaders DEPENDS ${GEN_DIR}/shaders_embedded.h)

# ---------------------------------------------------------------------------
# Core library shared by the app and the test binary.
# ---------------------------------------------------------------------------
add_library(bhcore STATIC
  src/blackbody.cpp
  src/geodesic.cpp
)
target_include_directories(bhcore PUBLIC src)

# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------
add_executable(blackhole
  src/main.cpp
  src/glutil.cpp
  src/renderer.cpp
  src/gridscene.cpp
  src/text.cpp
  src/png.cpp
)
add_dependencies(blackhole shaders)
target_include_directories(blackhole PRIVATE src ${GEN_DIR})
target_link_libraries(blackhole PRIVATE bhcore glfw GLEW::GLEW)

if(APPLE)
  target_link_libraries(blackhole PRIVATE
    "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

# ---------------------------------------------------------------------------
# Physics validation tests
# ---------------------------------------------------------------------------
add_executable(physics_tests tests/physics_tests.cpp)
target_link_libraries(physics_tests PRIVATE bhcore)
