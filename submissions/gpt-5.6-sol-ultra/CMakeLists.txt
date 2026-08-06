cmake_minimum_required(VERSION 3.20)

project(SchwarzschildLab VERSION 1.0.0 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR
        "This clean-room build currently targets macOS OpenGL 4.1. "
        "A non-Apple port needs an OpenGL function loader and platform packaging."
    )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(schwarzschild_lab MACOSX_BUNDLE
    src/main.cpp
    src/shaders.hpp
)
set_target_properties(schwarzschild_lab PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Schwarzschild Lab"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.solultra.schwarzschild-lab"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

find_file(GLFW_STATIC_ARCHIVE
    NAMES libglfw3.a
    HINTS /opt/homebrew/opt/glfw/lib /usr/local/opt/glfw/lib
)

if(GLFW_STATIC_ARCHIVE)
    get_target_property(GLFW_INCLUDE_DIRECTORIES glfw INTERFACE_INCLUDE_DIRECTORIES)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_include_directories(schwarzschild_lab PRIVATE ${GLFW_INCLUDE_DIRECTORIES})
    target_link_libraries(schwarzschild_lab PRIVATE
        "${GLFW_STATIC_ARCHIVE}"
        OpenGL::GL
        "${COCOA_FRAMEWORK}"
        "${IOKIT_FRAMEWORK}"
        "${COREFOUNDATION_FRAMEWORK}"
    )
else()
    target_link_libraries(schwarzschild_lab PRIVATE glfw OpenGL::GL)
endif()
target_compile_definitions(schwarzschild_lab PRIVATE GLFW_INCLUDE_NONE)

if(APPLE)
    target_compile_definitions(schwarzschild_lab PRIVATE GL_SILENCE_DEPRECATION)
    add_custom_command(TARGET schwarzschild_lab POST_BUILD
        COMMAND /usr/bin/xattr -cr "$<TARGET_BUNDLE_DIR:schwarzschild_lab>"
        COMMAND /usr/bin/codesign --force --deep --sign -
                "$<TARGET_BUNDLE_DIR:schwarzschild_lab>"
        COMMENT "Applying an ad-hoc signature to the local macOS app bundle"
        VERBATIM
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(schwarzschild_lab PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
endif()

option(BUILD_TESTING "Build the Schwarzschild regression tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_executable(physics_tests tests/physics_tests.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(physics_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
    add_test(NAME schwarzschild_physics COMMAND physics_tests)
endif()
