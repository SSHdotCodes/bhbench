cmake_minimum_required(VERSION 3.16)
project(black_hole_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

if(APPLE)
  add_compile_definitions(GL_SILENCE_DEPRECATION=1)
endif()

# ---------------------------------------------------------------------------
#  shaders are canonical on disk (shaders/*.vert|frag) and are embedded into
#  the binary at build time (generated/shaders_embedded.h) so the executable
#  also runs standalone.  At runtime the .glsl files win if found, which
#  makes them hot-reloadable while the sim is running.
# ---------------------------------------------------------------------------
set(EMBED_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders_embedded.h)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag)
add_custom_command(
  OUTPUT ${EMBED_OUT}
  COMMAND ${CMAKE_COMMAND} -DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/shaders -DOUT=${EMBED_OUT} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.cmake
  DEPENDS ${SHADER_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.cmake
  COMMENT "Embedding GLSL shader sources"
  VERBATIM)

# ---------------------------------------------------------------------------
#  OpenGL + windowing
# ---------------------------------------------------------------------------
add_executable(black-hole src/main.cpp ${EMBED_OUT})
target_include_directories(black-hole PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

target_compile_definitions(black-hole PRIVATE
  BLACKHOLE_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")

find_package(OpenGL REQUIRED)

set(GLFW_TARGET "")
find_package(glfw3 CONFIG QUIET)
if(glfw3_FOUND)
  set(GLFW_TARGET glfw)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GLFW3 QUIET IMPORTED_TARGET glfw3)
    if(GLFW3_FOUND)
      set(GLFW_TARGET PkgConfig::GLFW3)
    endif()
  endif()
endif()

if(NOT GLFW_TARGET)
  message(FATAL_ERROR "GLFW 3 not found. Install it with:  brew install glfw")
endif()

target_link_libraries(black-hole PRIVATE ${GLFW_TARGET} OpenGL::GL)

if(APPLE)
  target_link_libraries(black-hole PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

message(STATUS "OpenGL:   ${OPENGL_LIBRARIES}")
message(STATUS "GLFW:     ${GLFW_TARGET}")
