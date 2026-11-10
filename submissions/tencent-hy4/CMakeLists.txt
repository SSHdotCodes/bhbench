cmake_minimum_required(VERSION 3.16)
project(schwarzschild_blackhole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# ---- dependencies: GLFW3 (pkg-config) + GLM (header-only) + OpenGL ----------
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

add_executable(blackhole
  src/main.cpp
)

target_include_directories(blackhole PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${GLFW_INCLUDE_DIRS}
  /opt/homebrew/include       # Homebrew (Apple Silicon): glm headers
  /usr/local/include          # Homebrew (Intel) / manual installs
)
target_link_directories(blackhole PRIVATE ${GLFW_LIBRARY_DIRS})
target_compile_options(blackhole PRIVATE ${GLFW_CFLAGS_OTHER})

if(APPLE)
  target_compile_definitions(blackhole PRIVATE GL_SILENCE_DEPRECATION)
  target_link_libraries(blackhole PRIVATE
    ${GLFW_LIBRARIES}
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework QuartzCore"
  )
else()
  # Non-Apple: OpenGL::GL + a glad/glew style loader would be needed (see README).
  find_package(OpenGL REQUIRED)
  target_link_libraries(blackhole PRIVATE ${GLFW_LIBRARIES} OpenGL::GL)
endif()
