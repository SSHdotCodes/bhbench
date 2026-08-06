cmake_minimum_required(VERSION 3.20)

project(BlackHoleCppCodex LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

add_executable(BlackHoleSim MACOSX_BUNDLE
    src/main.mm
)

target_compile_options(BlackHoleSim PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-Wno-deprecated-declarations>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
)

find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)

target_link_libraries(BlackHoleSim PRIVATE
    ${COCOA_FRAMEWORK}
    ${OPENGL_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
)

set_target_properties(BlackHoleSim PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Black Hole Simulator"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.codex.black-hole-sim"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
)
