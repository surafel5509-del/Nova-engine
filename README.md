# Nova Engine

A 2D-first Android game engine and visual editor, built with Kotlin + Jetpack
Compose on the editor side and C++ (NDK) + OpenGL ES 3 on the engine side.
The architecture is staged so 3D (Phase 6) and Vulkan can be added without
rewriting the core.

**Status: Phase 1 (Foundation) complete; Play Mode upgrade complete.** See
[docs/PHASE1.md](docs/PHASE1.md) for the milestone report,
[docs/PLAYMODE.md](docs/PLAYMODE.md) for the play-mode upgrade, and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design.

## Layout

```
/app        Android application + editor UI (Compose)
  /assets     Asset browser store (file tree operations)
  /runtime    Standalone game runtime (GameActivity + GameSurfaceView)
/engine     C++ engine core (CMake; host-testable subset)
  /core       Engine lifecycle, simulation loop, input
  /scene      Render scene model + JSON parsing (nlohmann/json)
  /rendering  GLES3 renderer, sprite batch, grid, shaders, textures
  /physics    2D physics world (bodies, AABB colliders, restitution)
  /math       Mat4 (column-major, GL-compatible)
  /tests      Host C++ unit tests (ctest)
  /third_party  Vendored nlohmann/json 3.11.3 (MIT)
/platform/android/jni   JNI bridge between Kotlin and the engine
/docs       Architecture, setup, phase reports
```

## Golden path (working)

Create Project → Create Scene (template) → Add Entity → Add Sprite →
Import Texture → See Sprite in Viewport → Add Physics Body → **Press Play →
watch physics simulate** → Stop (scene restored) → Save → **Run ▶ (full-screen
game runtime)**.

APK export of *game projects* arrives in Phase 5; the editor app itself builds
to a signed-debug APK today.

## Build

Requirements: JDK 17+, Android SDK 34, NDK 26.1.10909125, CMake 3.22.1
(see docs/SETUP.md for exact setup steps).

```bash
./gradlew :app:assembleDebug       # editor APK (incl. libnovaengine.so)
./gradlew :app:testDebugUnitTest   # JVM unit tests (model, undo, camera, projects)
cd engine/tests && mkdir -p build-host && cd build-host \
  && cmake .. && make && ./nova_tests   # host C++ tests
```

## Controls

- Tap: select entity (world-accurate picking, rotation-aware)
- Drag entity: move (snap to 0.5 units when Snap is on)
- Two-finger drag: pan; pinch: zoom; Zoom tool: vertical drag zooms
- Toolbar: Save / Undo / Redo / Select / Move / Pan / Zoom / Grid / Snap /
  Play / Pause / Stop / Game view / Physics debug / Run ▶
- Keyboard (when attached): Ctrl+S, Ctrl+Z, Ctrl+Shift+Z, Ctrl+D, Delete,
  Q (select), W (move), G (game view), Space (play/pause)
