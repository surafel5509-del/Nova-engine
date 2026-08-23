# Nova Engine — Architecture (Phase 1)

## Big picture

```
┌────────────────────────── Android app (Kotlin) ──────────────────────────┐
│  ui/project (ProjectManagerScreen)   ui/editor (EditorScreen + panels)   │
│                              │                                           │
│                    editor/EditorViewModel                                │
│   owns: Scene (immutable), selection, Camera2D, UndoStack, console       │
│                              │                                           │
│   scene/Model.kt  scene serialization   project/ProjectRepository        │
│                              │  buildRenderScene() -> flat JSON          │
├──────────────────────────────┼───────────────────────────────────────────┤
│  bridge/NativeEngine (JNI) + bridge/EngineGlRenderer (GLSurfaceView)     │
├──────────────────────────────┼───────────────────────────────────────────┤
│  C++ engine (libnovaengine.so)                                           │
│   core/Engine → rendering/GlesRenderer → SpriteBatch / grid shader       │
│   scene/RenderScene (nlohmann/json)                                      │
└──────────────────────────────────────────────────────────────────────────┘
```

## Key decisions

1. **Kotlin owns the editor scene model.** Entities, components, hierarchy and
   undo/redo live in immutable Kotlin data classes. This keeps editor logic
   unit-testable on the JVM (no Android runtime needed) and makes Compose
   state trivial. The C++ engine receives a *flat, world-space* render scene
   (JSON) whenever the model changes — the editor and the runtime engine
   share one serialization format, and 3D later only adds new component
   fields/entity kinds, not a new pipeline.

2. **C++ owns rendering and (later) simulation.** GL ES 3 renderer with a
   dynamic sprite batcher, shader-based infinite grid, RGBA texture uploads.
   All GL work happens on the GLSurfaceView render thread; Kotlin pushes
   state via `queueEvent`. The renderer is deliberately interface-shaped so a
   Vulkan backend can be added beside it (Phase 6+).

3. **Commands everywhere.** Every mutation — create/delete/rename/reparent/
   component edit/drag-move — is an `EditorCommand` with execute/undo, run
   through one `UndoStack`. Continuous drags record one `SnapshotEntityCommand`
   at gesture end via `pushPreApplied`.

4. **Versioned serialization.** `scene.json` and `project.json` carry a
   `version` field; unknown fields are ignored (forward compatibility) and
   newer versions are rejected loudly. kotlinx.serialization on the Kotlin
   side, nlohmann/json (vendored) on the C++ side.

5. **Single Gradle module for now.** `app/` contains the editor UI
   (`ui/editor/*`). Splitting an `:editor` library module is deferred until
   the build graph benefits from it; the package layout already separates
   concerns.

## Data flow: editor change → pixels

1. User edits (gesture/inspector/hierarchy) → `EditorViewModel` applies an
   `EditorCommand` → new immutable `Scene`, `renderRevision++`.
2. `Viewport` observes `renderRevision` → `buildRenderScene(scene, selectedId)`
   flattens hierarchy (parent transform composition, enabled-chain culling,
   sortingOrder) → JSON → `NativeEngine.nativeSetScene`.
3. C++ `RenderScene.parseFrom` validates → `GlesRenderer.drawFrame`:
   clear → grid shader → sprite batch (per-texture flush) → selection outline.
4. Camera is Kotlin-owned (`Camera2D`, also used for picking) and pushed via
   `nativeSetViewport`; textures are decoded with `BitmapFactory` off the UI
   thread and uploaded as RGBA8888 with `nativeLoadTexture`.

## Scene model (Phase 1 components)

`Entity { id, name, enabled, parentId, transform, sprite?, camera?, physicsBody? }`

- **TransformComponent** — x, y, rotation (deg), scaleX/Y; world transform is
  composed parent-first (`SceneOps.compose`).
- **SpriteComponent** — size, RGBA tint, flips, sortingOrder, optional texture.
- **CameraComponent** — editor-game camera data (used by Play mode later).
- **PhysicsBodyComponent** — body type/mass/friction/etc. Data model only;
  simulation arrives with Play mode (Phase 4).

## Threading

- UI thread: Compose, ViewModel state.
- GL thread: all native calls (via `GLSurfaceView.queueEvent`); the renderer
  re-applies cached scene/viewport/textures after context recreation.
- IO dispatcher: texture file reads + bitmap decode.

## Testing strategy

- JVM unit tests cover the parts users depend on: scene ops, hierarchy,
  transform composition, serialization (round-trip, versioning, forward
  compatibility), undo/redo (including subtree delete and pre-applied drags),
  camera math (inverse conversions, zoom-at stability, clamps), picking hit
  test, and the project repository (real temp-dir file IO, no mocks).
- Host C++ tests (ctest) cover JSON scene parsing (valid, defaults, errors),
  Mat4, and quad corner math — the GL-free engine subset.
- No emulator is available in this environment; runtime/on-device verification
  is listed as a known issue in docs/PHASE1.md.
