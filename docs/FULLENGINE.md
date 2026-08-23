# Full Engine Upgrade Report (Play Mode → Full Game Engine)

Milestone: upgrade the editor+engine from Play Mode to a complete game stack:
GPU particles, tilemaps, audio, **Lua game scripting**, live profiler, script
editor, and **standalone game APK export**.

## New systems

### Scripting (Lua 5.4, real)

- `engine/scripting/LuaScriptEngine.{h,cpp}` — vendored Lua 5.4.7 (MIT), one VM
  per simulation. Scripts push source to the engine; per-script environments;
  lifecycle `on_start(id)` / `on_update(id, dt)`.
- `nova` API table: `get_position`, `set_position`, `get_velocity`,
  `set_velocity`, `is_grounded`, `input_axis`, `input_jump`,
  `set_animation_frame`, `play_sound`, `log`.
- Bound via `ScriptComponent` (Inspector → Script) and the **Scripts** editor
  tab (file list, new-from-template, save, attach). Errors route to the
  Console; `nova.log` lines route to the Console too.
- Host-tested (15 checks): input→velocity, sound events, error capture,
  environment isolation between scripts.

### Particles

- `engine/particles/ParticleSystem.{h,cpp}` — CPU pool with deterministic RNG,
  emission rate/lifetime/speed/spread/gravity/size-over-life; rendered as
  sprites in the batch. Edited via ParticleEmitter component.
- Host-tested (9 checks): emission, culling, gravity, clear, burst toggle.

### Tilemaps

- `TilemapComponent` (grid of atlas indices) rendered natively via the sprite
  batcher's UV sub-rect path. TILE tool paints/erases in the viewport with an
  adjustable brush index (Inspector). Undoable per cell.
- Rendered below sprites; tileset texture optional (colored cells).

### Audio

- `AudioEngine` (Kotlin): SoundPool for SFX (volume/pitch/loop), MediaPlayer
  for music streams. Autoplay sources start on Play; script `play_sound`
  events route through the native event queue each frame.
- AudioSource component: clip path, volume, pitch, loop, autoplay, music.

### Profiler

- Native stats (JSON): fps, frame ms, draw calls (counted in SpriteBatch),
  sprites, bodies, particles, scripts. JVM heap appended Compose-side.
- Toggle with the **Stats** chip; HUD overlay on the viewport.

### Build/export

- `BuildExporter` (pure JVM, tested): packages project content into
  `.novapkg` zip. Build → Export dialog shows the exact Gradle command:
  `./gradlew :game:assembleDebug -PnovaProjectPath=<path>`.
- `:game` module: self-contained APK with the engine, runtime surface, scene
  model, audio, and the project packaged into `assets/project/`. Verified with
  `samples/platformer` (scene + Lua controller).

## Test results

- Host C++: **66 checks, 0 failures** (scene/math 34, physics 8, particles 9,
  scripting 15).
- JVM: **67 tests, 0 failures** (TilemapTest, TilePaintTest, BuildExporterTest
  + existing suites).
- Builds: `:app:assembleDebug` OK (libnovaengine.so with vendored Lua);
  `:game:assembleDebug` OK (standalone game APK, sample platformer packaged).

## Known limitations / next steps

- 2D lighting, tile auto-tiling/terrain, joints/raycasts: future increments.
- The line-number column in the script editor is an approximate-scroll helper
  (editing + attach are fully functional).
- 3D remains Phase 6 by design (architecture staged).
