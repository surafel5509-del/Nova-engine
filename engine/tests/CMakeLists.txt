cmake_minimum_required(VERSION 3.16)
project(nova_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Host-side unit tests for the platform-independent engine subset
# (scene parsing, math, geometry). GL/JNI code is excluded.

add_executable(nova_tests
    test_main.cpp
    test_physics.cpp
    ../scene/RenderScene.cpp
    ../physics/PhysicsWorld.cpp
)

target_include_directories(nova_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)

enable_testing()
add_test(NAME nova_tests COMMAND nova_tests)
