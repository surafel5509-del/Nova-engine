# Nova Engine

A 2D-first Android game engine and visual editor, built with Kotlin + Jetpack
Compose on the editor side and C++ (NDK) + OpenGL ES 3 on the engine side.
The architecture is staged so 3D (Phase 6) and Vulkan can be added without
rewriting the core.

**Status: Professional 2D + 3D game engine + autonomous AI agent.**
Foundation → Play Mode (physics) → particles, tilemaps, audio, **Lua
scripting**, profiler HUD, script editor, **standalone game APK export** →
**AI game-builder** (Gemini / ChatGPT / Claude / DeepSeek / custom), **game UI
builder**, raycast, camera follow, auto-tiling → **complete 3D engine**,
**autonomous AI game-development agent** (planning, step execution,
verification, timing), **file manager + Assets Library with ZIP import**,
**animation timeline**, and **desktop/mobile settings**. See
[docs/PROENGINE.md](docs/PROENGINE.md), [docs/AIBUILDER.md](docs/AIBUILDER.md),
[docs/FULLENGINE.md](docs/FULLENGINE.md), and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Layout

```
/app        Android application + editor UI (Compose)
  /ai         AI game-builder + autonomous agent (planning, execution, verify)
  /assets     Asset store (tree ops + safe ZIP import)
  /audio      AudioEngine (SoundPool SFX + MediaPlayer music)
  /build      BuildExporter (pure-JVM .novapkg packaging)
  /settings   Editor settings (desktop/mobile mode, graphics, audio, perf)
  /runtime    GameActivity (Compose headless-game launcher in editor)
  /gameruntime/GameSurfaceView  Shared full-screen game surface
  /ui         UiTextTexture (text -> bitmap for native UI)
  /ui/editor  Hierarchy, Inspector, Viewport, FileManager, AssetsLibrary,
              Timeline, ScriptEditor, AI panel, Profiler, Console, StatusBar
  /ui/settings  Settings screen
/engine     C++ engine core (CMake; host-testable subset)
  /core       Engine lifecycle, simulation loop, input, stats, UI hit-test
  /scene      Render scene + JSON parsing + AnimationSampler (nlohmann/json)
  /rendering  2D GLES3 renderer + 3D renderer (meshes, lighting, grid, axes)
  /physics    2D physics world (bodies, AABB colliders, restitution, raycast)
  /particles  CPU particle system (deterministic RNG)
  /scripting  Lua 5.4 VM + nova.* API bindings
  /math       Mat4 (2D ortho + 3D perspective/lookAt/rotations)
  /tests      Host C++ unit tests: scene, physics, raycast, particles, Lua, 3D
  /third_party  nlohmann/json 3.11.3, Lua 5.4.7 (both MIT)
/game       Standalone game APK module (packaged project in assets)
/samples    Three complete sample games (platformer, space-shooter, brick-breaker)
/platform/android/jni   JNI bridge between Kotlin and the engine
/docs       Architecture, setup, milestone reports
```

## Golden path (working)

Create Project (**2D or 3D**) → Create Scene → Add Entity (or **ask the AI
agent** — it plans, builds, and verifies step by step) → Add Sprite / Mesh /
Tilemap / Emitter / Audio / **UI** / Light → Import assets (or **ZIP into the
Assets Library**) → Edit animation in the **Timeline** → **Press Play →
physics + particles + Lua scripts + audio + game UI + animations run** →
Build → **Run ▶ or export a standalone game APK (`:game` module)**.

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
