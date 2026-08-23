# Nova Engine

A 2D-first Android game engine and visual editor, built with Kotlin + Jetpack
Compose on the editor side and C++ (NDK) + OpenGL ES 3 on the engine side.
The architecture is staged so 3D (Phase 6) and Vulkan can be added without
rewriting the core.

**Status: Full game-engine stack.** Foundation → Play Mode (physics) →
particles, tilemaps, audio, **Lua scripting**, profiler HUD, script editor,
and **standalone game APK export** — all real, all tested. See
[docs/FULLENGINE.md](docs/FULLENGINE.md), [docs/PLAYMODE.md](docs/PLAYMODE.md),
and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Layout

```
/app        Android application + editor UI (Compose)
  /assets     Asset browser store (file tree operations)
  /audio      AudioEngine (SoundPool SFX + MediaPlayer music)
  /build      BuildExporter (pure-JVM .novapkg packaging)
  /runtime    GameActivity (Compose headless-game launcher in editor)
  /gameruntime/GameSurfaceView  Shared full-screen game surface
/engine     C++ engine core (CMake; host-testable subset)
  /core       Engine lifecycle, simulation loop, input, stats
  /scene      Render scene model + JSON parsing (nlohmann/json)
  /rendering  GLES3 renderer, sprite batch, grid, shaders, textures
  /physics    2D physics world (bodies, AABB colliders, restitution)
  /particles  CPU particle system (deterministic RNG)
  /scripting  Lua 5.4 VM + nova.* API bindings
  /math       Mat4 (column-major, GL-compatible)
  /tests      Host C++ unit tests: scene, physics, particles, Lua
  /third_party  nlohmann/json 3.11.3, Lua 5.4.7 (both MIT)
/game       Standalone game APK module (packaged project in assets)
/samples    Sample projects (platformer used for :game export tests)
/platform/android/jni   JNI bridge between Kotlin and the engine
/docs       Architecture, setup, milestone reports
```

## Golden path (working)

Create Project → Create Scene (template) → Add Entity → Add Sprite /
Tilemap / Emitter / Audio → Import Assets → See elements in Viewport →
Add Physics Body / Script → **Press Play → physics + particles + Lua scripts
+ audio run** → Build → **Run ▶ or export a standalone game APK
(`:game` module)**.

## Build

Requirements: JDK 17+, Android SDK 34, NDK 26.1.10909125, CMake 3.22.1
(see docs/SETUP.md for exact setup steps).

```bash
./gradlew :app:assembleDebug       # editor APK (incl. libnovaengine.so + Lua)
./gradlew :app:testDebugUnitTest   # JVM unit tests (model, undo, tiles, build, camera)
./gradlew :game:assembleDebug -PnovaProjectPath=samples/platformer   # game APK
cd engine/tests && mkdir -p build-host && cd build-host \
  && cmake .. && make && ./nova_tests   # host C++ tests (incl. Lua scripting)
```

## Controls

- Tap: select entity (world-accurate picking, rotation-aware)
- TILE tool: paint/erase tiles with the brush index
- Drag entity: move (snap to 0.5 units when Snap is on)
- Two-finger drag: pan; pinch: zoom; Zoom tool: vertical drag zooms
- Toolbar: Save / Undo / Redo / Select / Move / Tile / Pan / Zoom / Grid /
  Snap / Play / Pause / Stop / Game view / Physics debug / Stats / Run ▶ / Build
- Keyboard (when attached): Ctrl+S, Ctrl+Z, Ctrl+Shift+Z, Ctrl+D, Delete,
  Q (select), W (move), G (game view), Space (play/pause)
