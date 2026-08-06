cmake_minimum_required(VERSION 3.20)

project(BlackHoleSolxHigh VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

if(APPLE)
    add_executable(black-hole-sim MACOSX_BUNDLE src/main.cpp)
else()
    add_executable(black-hole-sim src/main.cpp)
endif()
target_link_libraries(black-hole-sim PRIVATE glfw OpenGL::GL)
target_compile_definitions(black-hole-sim PRIVATE
    BH_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shaders/black_hole.frag"
)

if(APPLE)
    target_compile_definitions(black-hole-sim PRIVATE GL_SILENCE_DEPRECATION)
    set_target_properties(black-hole-sim PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "SolxHigh Black Hole"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.solxhigh.blackhole"
        MACOSX_BUNDLE_INFO_STRING "Real-time Schwarzschild null-geodesic ray tracer"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(black-hole-sim PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(geodesic-tests tests/geodesic_tests.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(geodesic-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME SchwarzschildGeodesics COMMAND geodesic-tests)
