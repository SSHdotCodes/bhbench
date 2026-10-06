cmake_minimum_required(VERSION 3.20)
project(BlackHoleRelativityLab VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
add_executable(black_hole src/main.cpp)
target_link_libraries(black_hole PRIVATE glfw OpenGL::GL)
target_compile_definitions(black_hole PRIVATE BH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(APPLE)
  target_compile_definitions(black_hole PRIVATE GL_SILENCE_DEPRECATION)
endif()
target_compile_options(black_hole PRIVATE -Wall -Wextra -Wpedantic)
add_executable(physics_tests tests/physics_tests.cpp)
target_include_directories(physics_tests PRIVATE src)
target_compile_options(physics_tests PRIVATE -Wall -Wextra -Wpedantic)
enable_testing()
add_test(NAME relativistic_physics COMMAND physics_tests)
add_test(NAME gpu_geodesic_reference COMMAND black_hole --validate-gpu)
if(APPLE)
  set(BH_APP "${CMAKE_CURRENT_SOURCE_DIR}/Black Hole.app/Contents")
  add_custom_target(black_hole_app ALL
    DEPENDS black_hole
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BH_APP}/MacOS" "${BH_APP}/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:black_hole>" "${BH_APP}/MacOS/black_hole"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist" "${BH_APP}/Info.plist"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${BH_APP}/Resources/shaders"
    VERBATIM)
endif()
