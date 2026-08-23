# Play Mode Upgrade Report

Milestone: turn the Phase-1 editor into a working game engine loop —
real Play mode with 2D physics, sprite-sheet animation, a functional asset
browser, a game view, and a standalone runtime.

## Implemented

### C++ engine
- `engine/physics/PhysicsWorld.{h,cpp}` — 2D physics: static/dynamic/kinematic
  bodies, AABB colliders, gravity integration, restitution (bounce), friction,
  positional separation. Host-testable (no GL).
- `engine/scene/RenderScene` — extended flat model: per-sprite
  `sortingOrder`, `parallaxFactor`, sprite-sheet frame grid
  (`frameCols/frameRows/frameIndex`); `bodies[]`; `gameCamera`.
- `engine/rendering/SpriteBatch` — UV sub-rects for sprite-sheet frames;
  `drawLineBox` for outlines/debug.
- `engine/rendering/GlesRenderer` — parallax offset per sprite, game-camera
  frame preview, physics debug rendering (collider outlines).
- `engine/core/Engine` — rewritten: owns the simulation. `startSimulation`
  snapshots the render scene into the physics world; `stepSimulation(dt)`
  integrates physics, writes positions back into the render scene, and advances
  animation frames; game-camera mode; input axis/jump state.
- `platform/android/jni/bridge.cpp` — new entry points: simulation
  start/stop/step/snapshot, game-camera/physics-debug flags, input axis/jump.

### Kotlin / editor
- `scene/Model.kt` — new components: `AnimatorComponent`,
  `ParticleEmitterComponent`; `SpriteComponent` gained `parallaxFactor`;
  `PhysicsBodyComponent` gained `colliderWidth/Height`; `CameraComponent`
  gained frustum + background. New entity kinds: `ANIMATED_SPRITE`,
  `PARTICLE_SYSTEM`. `buildRenderScene` now emits bodies + game camera.
- `editor/EditorViewModel` — `PlayState` (STOPPED/PLAYING/PAUSED), play()
  snapshots the scene, stop() restores it, `applySimulatedPositions` guard,
  game-view/physics-debug toggles, asset-browser state, texture assignment.
- `ui/editor/Viewport.kt` — play loop: starts the native simulation, steps it
  at ~60 Hz with clamped dt; the engine owns the moving scene during play.
- `ui/editor/EditorScreen.kt` — functional Play/Pause/Stop buttons, Game and
  Physics toggle chips, Run ▶ (launches GameActivity), Space/G shortcuts,
  Assets bottom tab.
- `ui/editor/InspectorPanel.kt` — editors for Animator, Particle Emitter,
  collider size, camera frustum/background, parallax; add/remove component
  buttons for all five component types.
- `assets/AssetStore.kt` + `ui/editor/AssetBrowserPanel.kt` — real file-tree
  browser: navigate, create folder, delete, texture/audio/scene detection,
  "Use" assigns a texture to the selected entity.
- `runtime/GameActivity.kt` + `runtime/GameSurfaceView.kt` — standalone
  full-screen runtime: loads the project scene, runs the game camera, auto-
  starts simulation, touch = input axis (left half) + jump (right half).
- Templates: Platformer (ground + falling player), RPG (animated hero +
  particle torch), Arcade (bouncing ball + floor) — all playable via Play.

## Test results

- JVM unit tests: **53 total, 0 failures** across 7 classes
  (new: `PlayModeTest` 5, `RenderSceneTest` 8, `AssetStoreTest` 7;
  updated `ProjectRepositoryTest` for the new templates).
- Host C++ tests: **31 checks, 0 failures** (scene/math 23, physics 8).
- `:app:assembleDebug` — BUILD SUCCESSFUL; `libnovaengine.so` packaged for
  arm64-v8a and x86_64.

## Architecture notes

- During play the **engine owns the moving scene**: physics writes positions
  directly into the native render scene, so no per-frame JSON round-trip is
  needed. Kotlin only re-reads positions if a future tool needs them
  (`nativeSnapshotPositions` is exposed for that).
- Stop restores the exact pre-play scene from a Kotlin-side snapshot, so play
  is always non-destructive and never touches the undo stack.

## Known limitations / next steps

- Particles: data model + inspector are in place; GPU emission draws in a
  follow-up (currently the emitter renders as its source sprite).
- Character-controller response to input (walk/jump forces) is wired at the
  input layer; per-body behavior flags are the next increment.
- Audio, tilemaps, 2D lighting: Phase 2 scope, architecture-ready.
- Build/export of game APKs: Phase 5.
