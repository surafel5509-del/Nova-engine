cmake_minimum_required(VERSION 3.22.1)
project(novaengine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When invoked from Gradle (AGP externalNativeBuild), ANDROID is defined.
# For host builds (unit tests), see engine/tests/CMakeLists.txt which
# compiles only the platform-independent subset.

# Vendored Lua 5.4.7 (MIT) for game scripting.
file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/*.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua)

add_library(novaengine SHARED
    core/Engine.cpp
    scene/RenderScene.cpp
    scene/AnimationSampler.cpp
    physics/PhysicsWorld.cpp
    particles/ParticleSystem.cpp
    scripting/LuaScriptEngine.cpp
    rendering/Shader.cpp
    rendering/Texture.cpp
    rendering/SpriteBatch.cpp
    rendering/GlesRenderer.cpp
    rendering/Mesh3D.cpp
    rendering/Renderer3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform/android/jni/bridge.cpp
)

target_include_directories(novaengine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

find_library(lib_log log)
find_library(lib_android android)
find_library(lib_gles GLESv3)
find_library(lib_egl EGL)

target_link_libraries(novaengine PRIVATE
    lua
    ${lib_gles}
    ${lib_egl}
    ${lib_android}
    ${lib_log}
)
