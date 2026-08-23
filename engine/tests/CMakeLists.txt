cmake_minimum_required(VERSION 3.16)
project(nova_host_tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Host-side unit tests for the platform-independent engine subset
# (scene parsing, math, physics, particles, Lua scripting). GL/JNI excluded.

file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/lua/*.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/lua)

add_executable(nova_tests
    test_main.cpp
    test_physics.cpp
    test_particles.cpp
    test_scripting.cpp
    ../scene/RenderScene.cpp
    ../physics/PhysicsWorld.cpp
    ../particles/ParticleSystem.cpp
    ../scripting/LuaScriptEngine.cpp
)

target_include_directories(nova_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)

target_link_libraries(nova_tests PRIVATE lua)

enable_testing()
add_test(NAME nova_tests COMMAND nova_tests)
