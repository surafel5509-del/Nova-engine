# Nova Engine

A 2D-first Android game engine and visual editor, built with Kotlin + Jetpack
Compose on the editor side and C++ (NDK) + OpenGL ES 3 on the engine side.
The architecture is staged so 3D (Phase 6) and Vulkan can be added without
rewriting the core.

**Status: Phase 1 (Foundation) complete.** See [docs/PHASE1.md](docs/PHASE1.md)
for the full milestone report and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
for the design.

## Layout

```
/app        Android application + editor UI (Compose)
/engine     C++ engine core (CMake; host-testable subset)
  /core       Engine lifecycle, logging
  /scene      Render scene model + JSON parsing (nlohmann/json)
  /rendering  GLES3 renderer, sprite batch, grid, shaders, textures
  /math       Mat4 (column-major, GL-compatible)
  /tests      Host C++ unit tests (ctest)
  /third_party  Vendored nlohmann/json 3.11.3 (MIT)
/platform/android/jni   JNI bridge between Kotlin and the engine
/docs       Architecture, setup, phase reports
```

## Golden path (working)

Create Project → Create Scene (template) → Add Entity → Add Sprite →
Import Texture → See Sprite in Viewport → Add Physics Body → Save.

Play mode, game view and APK export of *game projects* arrive in Phases 4–5;
the editor app itself builds to a signed-debug APK today.

## Build

Requirements: JDK 17+, Android SDK 34, NDK 26.1.10909125, CMake 3.22.1
(see docs/SETUP.md for exact setup steps).

```bash
./gradlew :app:assembleDebug       # editor APK (incl. libnovaengine.so)
./gradlew :app:testDebugUnitTest   # JVM unit tests (model, undo, camera, projects)
cd engine/tests && mkdir -p build-host && cd build-host \
  && cmake .. && make && ./nova_tests   # host C++ tests
```

## Controls (Phase 1)

- Tap: select entity (world-accurate picking, rotation-aware)
- Drag entity: move (snap to 0.5 units when Snap is on)
- Two-finger drag: pan; pinch: zoom; Zoom tool: vertical drag zooms
- Toolbar: Save / Undo / Redo / Select / Move / Pan / Zoom / Grid / Snap
- Keyboard (when attached): Ctrl+S, Ctrl+Z, Ctrl+Shift+Z, Ctrl+D, Delete, Q, W
