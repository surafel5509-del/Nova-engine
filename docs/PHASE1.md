# Phase 1 — Foundation: milestone report

## Implemented features

**Project Manager**
- Recent projects list (sorted by last opened), create, open, delete (with confirm).
- Create-project dialog: name, package name, version, orientation,
  dimension (2D; 2D+3D/3D shown disabled as Phase 6), template picker.
- Templates: Empty 2D (camera entity) and Platformer (camera + ground +
  dynamic-body player) generate real scenes; RPG/Arcade shown disabled as Phase 2+.
- Project structure on disk: `project.json`, `scenes/main.scene.json`,
  `assets/textures`, `assets/audio`, `prefabs`, `scripts`, `shaders`, `materials`.
- Import-from-external is explicitly marked Phase 5.

**Editor shell**
- Dark theme; responsive layout: 3-pane (hierarchy | viewport+console | inspector)
  on landscape/tablet (≥840dp or wider-than-tall), viewport + bottom tabbed
  panels on portrait phones.
- Top toolbar: Back (auto-saves), Save (dirty-aware), Undo, Redo,
  Select/Move/Pan/Zoom tools, Grid toggle, Snap toggle.
- Play/Pause/Stop and Build are visible but disabled and labeled with their
  phase — nothing pretends to work.
- Keyboard shortcuts (attached keyboards): Ctrl+S, Ctrl+Z, Ctrl+Shift+Z,
  Ctrl+D, Delete, Q (select), W (move).

**Scene hierarchy**
- Create (Empty/Sprite/Camera/Physics Body), rename, duplicate (subtree,
  fresh ids, unique names), delete (subtree), reparent (cycle-safe),
  add-child, enable/disable toggle, collapse/expand, search filter.
- Sibling order and depth come from the flat entity list + parentId.

**Inspector (foundation)**
- Transform: position/rotation/scale numeric fields, reset.
- Sprite: width/height, RGBA sliders, flips, sorting order, texture import
  (system picker → project `assets/textures` → GL upload) and clear, reset, remove.
- Camera: zoom field. Physics body: type dropdown, mass/gravity/friction/
  restitution (data model only; simulation is Phase 4).
- Add Component (Sprite/Camera/Physics) when absent. All edits are undoable.

**2D Viewport**
- Native GLES 3 surface: shader-based infinite grid (1u minor / 10u major,
  zoom-aware fading), red/green world axes, dark background.
- Pan (drag empty space / two fingers / Pan tool), smooth pinch zoom anchored
  at the focus point, Zoom tool (vertical drag), zoom clamps.
- Tap-to-select with rotation-aware world picking; selected entity gets a
  cyan outline; entity dragging with optional 0.5-unit snapping.
- Textured sprites (RGBA8888 upload) with alpha blending, tint color,
  per-texture batch flushing; untextured sprites render as tinted quads.

**C++ engine bridge & lifecycle**
- `libnovaengine.so` (CMake/NDK): Engine facade, GLES3 renderer, sprite batch,
  shader/texture wrappers, nlohmann/json render-scene parsing, Logcat logging.
- JNI bridge (`platform/android/jni/bridge.cpp`) for lifecycle, scene JSON,
  viewport, grid, textures; GL context loss recovery re-applies cached state.

**Scene/entity model & serialization**
- Immutable Kotlin model, versioned JSON (`version: 1`), unknown-field
  tolerance, future-version rejection, parent-chain transform composition,
  enabled-chain culling, sorting-order-aware render flattening.

**Undo/redo**
- Centralized `UndoStack`; commands for add/delete/rename/update/reparent/
  duplicate; drags recorded as single snapshot commands; capacity-bounded.

## Files created

- Root: `settings.gradle.kts`, `build.gradle.kts`, `gradle.properties`,
  `.gitignore`, `local.properties` (git-ignored), `README.md`, Gradle wrapper.
- `app/`: `build.gradle.kts`, `proguard-rules.pro`, `AndroidManifest.xml`,
  launcher icon + theme resources.
- Kotlin (`app/src/main/java/dev/nova/editor/`): `MainActivity`,
  `ui/NovaApp`, `ui/theme/Theme`, `ui/project/ProjectManagerScreen`,
  `ui/editor/{EditorScreen,HierarchyPanel,InspectorPanel,ConsolePanel,Viewport,Dialogs}`,
  `scene/Model.kt`, `editor/{EditorViewModel,Undo,Camera2D}.kt`,
  `project/ProjectRepository.kt`, `bridge/{NativeEngine,EngineGlRenderer}.kt`.
- C++ (`engine/`): `core/{Engine,Log}`, `scene/RenderScene`,
  `rendering/{GlesRenderer,SpriteBatch,Shader,Texture,Geometry}`, `math/Mat4`,
  `CMakeLists.txt`, `third_party/nlohmann/json.hpp` (vendored 3.11.3).
- JNI: `platform/android/jni/bridge.cpp`.
- Tests: `app/src/test/...` (4 suites, 24 tests), `engine/tests/` (host ctest).
- Docs: `docs/{ARCHITECTURE,SETUP,PHASE1}.md`.

## Build result

- `./gradlew :app:assembleDebug` — **success**
  (`app-debug.apk`, `libnovaengine.so` for arm64-v8a + x86_64 packaged).
- `./gradlew :app:assembleRelease` — **success** (unsigned; signing is Phase 5).
- Zero Kotlin/Java/C++ warnings in the build log.

## Tests

- JVM: 24/24 pass (`:app:testDebugUnitTest`) — scene ops, serialization,
  undo/redo, camera math, project repository (real temp-dir IO, no mocks).
- Host C++: 23/23 checks pass (`engine/tests/build-host/nova_tests`) —
  render-scene parsing, Mat4, quad geometry.

## Known issues / limitations

- **No emulator/device in this environment** — the app compiles, packages and
  unit tests pass, but on-device rendering (grid/sprite visuals, gesture feel)
  has not been runtime-verified. First on-device run may surface GL driver
  quirks (e.g. `fwidth` in the grid shader needs `#extension` on rare drivers,
  wide lines are clamped to 1px on most GPUs).
- Selection outlines are `GL_LINE_STRIP` (1px on many devices); a quad-based
  thick outline is a Phase 2 polish item.
- Sprite batching flushes per texture (correct but unbatched until atlas
  support lands in Phase 2).
- Viewport renders continuously (`RENDERMODE_CONTINUOUSLY`); switch to
  dirty-flag rendering for battery in a later phase.
- Hierarchy reparent is dialog-based; drag/drop reordering is Phase 3.
- Textures are not evicted from GL when unreferenced; `nativeRemoveTexture`
  exists but the UI doesn't call it yet.
- `assembleRelease` is unsigned and unminified by design for Phase 1.

## Recommended next phase

**Phase 2 — Core 2D:** texture atlas + true sprite batching, asset browser
(grid/list over `assets/`), sprite animation component + editor, tilemap
component + tile tools, orthographic game-camera preview, 2D physics
simulation wired to the existing PhysicsBody data model, particle system,
audio playback, then 2D lighting.
