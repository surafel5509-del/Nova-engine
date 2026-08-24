# Full Professional Engine Upgrade Report

Milestone: the biggest single upgrade — a complete 3D engine, an autonomous
AI game-development agent, a real file manager, an Assets Library, an
animation timeline, settings with desktop/mobile mode, and 3D project
creation — matching the professional game-editor reference.

## 3D game engine (real, native)

- `engine/rendering/Mesh3D.{h,cpp}` — primitive mesh generation (cube,
  cylinder, ground plane) with normals; host-testable.
- `engine/rendering/Renderer3D.{h,cpp}` — perspective orbit camera
  (Mat4::perspective + lookAt), Lambert-lit meshes with directional light +
  ambient, ground grid + RGB world axes, selection highlight, depth test.
- `Mat4` — added perspective, lookAt, rotationX/Y/Z, 3D transform.
- Scene model: `MeshComponent` (cube/cylinder/ground/plane + color),
  `LightComponent` (directional + ambient), `mode3d` flag, 3D render
  projection (`RenderObject3D`, `RenderLight`).
- Kotlin `Camera3D` — orbit (drag), dolly (pinch), pan (2-finger), plus
  screen-space ray construction; 3D picking via ray-AABB.
- 3D project creation (Project Manager → 3D) with a starter scene (ground,
  sun light, player cube, enemy cube, cylinder pillar).

## Autonomous AI game-development agent

- `ai/AgentRunner.kt` — the agent: (1) asks the LLM for a numbered plan,
  (2) executes each task with a task-specific prompt, (3) applies actions as
  one undoable edit, (4) verifies (scene changed, scripts valid), (5) retries
  failures once, (6) logs every step with mm:ss elapsed time.
- AI panel shows the plan as a live numbered checklist (○ pending, ▶ running,
  ✓ done, ✗ failed) with per-task timing and an agent log.
- New **🤖 Agent** button alongside the one-shot "Build with AI".

## File manager + Assets Library

- `FileManagerPanel` — full project file tree: navigate, create folder/file,
  rename, move, delete, and **import complete game-source ZIPs**.
- `AssetsLibraryPanel` — searchable grid of resources under `assets/library/`
  with **texture previews**, audio/script icons, **ZIP import**, and "Use"
  to assign textures to the selection. Assets auto-appear on refresh.
- `AssetStore.importZip` — safe extraction with path-traversal guard
  (lexical `..` rejection + canonical path check); `createFile`, `move`.

## Animation timeline

- `AnimationClipComponent` with tracks (x, y, rotation, scaleX, scaleY) and
  keyframes, sampled natively during Play (`AnimationSampler`, loop/clamp).
- `TimelinePanel` — time ruler, per-track rows with keyframe markers, add
  track (seeded from current values), add/remove keyframes and tracks.

## Settings + responsive UI

- `SettingsStore` + `SettingsScreen` — **Desktop/Window vs Mobile mode**
  (actually switches the editor layout), display (grid, vsync), audio (master
  volume), performance (target fps). Accessible from the Project Manager.
- `EditorScreen` honors the layout mode (desktop = always 3-pane, mobile =
  adaptive tabs).

## Test results

- Host C++: **89 checks, 0 failures** (3D/animation 19: meshes, perspective,
  lookAt, 3D scene parse, animation sampling).
- JVM: **110 tests, 0 failures** (Camera3DTest 6, AgentRunnerTest 3,
  Render3DTest 5, AssetZipTest 2 + all existing).
- Builds: `:app:assembleDebug` + `:game:assembleDebug` — both green, both
  ship libnovaengine.so for 3 ABIs.

## Known limitations / next steps

- 3D picking uses axis-aligned bounding boxes (no rotation-aware mesh pick).
- 3D meshes don't yet support textures/materials (reserved fields).
- The AI agent runs one plan pass; a second "verify & fix" refinement loop
  with play-testing is the next increment.
- Auto-tile terrain-rule editing, 2D lighting, physics joints: future.
