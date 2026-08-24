# AI Game Builder + UI Builder + Full Tools Report

Milestone: the AI game-builder assistant, the game UI builder, and a round of
"Godot/Unity-class" tool upgrades — raycasts, camera follow, sprite flip,
auto-tiling, status bar, and three complete sample games.

## AI game builder (multi-provider, custom API keys)

- `ai/AiClient.kt` — one client for **Google Gemini, OpenAI (ChatGPT),
  Anthropic Claude, DeepSeek, and any OpenAI-compatible endpoint** (local
  servers, proxies). Request building and response parsing are pure functions
  (JVM-tested); only `chat()` touches the network. `INTERNET` permission added.
- `ai/AiSettingsStore.kt` — provider/base-url/model/API-key persisted in
  SharedPreferences (keys stay on-device).
- `ai/AiActionApplier.kt` — the AI returns a JSON action protocol
  (create_entity, set_physics, add_script, set_transform, add_particles);
  actions are applied to the scene as **one undoable edit** and scripts are
  written to the project's `scripts/` dir.
- `ui/editor/AiAssistantPanel.kt` — the **AI tab**: provider settings dialog,
  prompt box, quick-idea prompts, busy indicator, last-reply view.
- The system prompt teaches the full Nova action protocol + the Lua API.

## Game UI builder

- `UiComponent` (label / button / panel) anchored to the camera center.
- Text is rendered to a bitmap via Android Canvas (`ui/UiTextTexture.kt`) and
  drawn by the native engine as a texture — no native font stack needed.
- During Play, taps hit-test UI elements natively (`Engine::onTap`) and reach
  scripts via `nova.ui_pressed(id)`. Scripts update labels with
  `nova.set_ui_text(id, text)` — an event queue round-trips to Kotlin, which
  re-renders the texture.
- Full Inspector editor: kind, text, font size, offsets, size, background color.

## Engine upgrades

- **Raycast** — slab-method AABB raycast in PhysicsWorld (host-tested) +
  `nova.raycast(x1,y1,x2,y2) -> id, hx, hy`.
- **Camera follow** — `CameraComponent.followTargetName` + smoothing lerp;
  the native engine tracks the target every frame during play/runtime.
- **Sprite flip** — `flipX`/`flipY` now actually flip UVs in the batcher.
- **Auto-tiling** — 4-neighbor bitmask blob tiling (tiles 0..15) with an
  AutoTile toolbar chip; painting recomputes the cell + neighbors (JVM-tested).

## UI/UX polish

- **Status bar** (scene name, entity count, active tool, play state, saved
  state) across the bottom of the editor.
- All new tools follow the existing dark theme (NovaColors).

## Three sample games (`samples/`)

- **platformer** — input movement, jump (axis + on-screen JUMP button),
  coin pickup with score label, camera follow, particle dust.
- **space-shooter** — ship dodges falling asteroids, FIRE button zaps them,
  dodge counter, starfield particles.
- **brick-breaker** — paddle + physics ball (restitution 1), 5 bricks broken
  on contact, launch button, score label.

Each is a real, playable project (scene JSON + Lua scripts) and all three
package into standalone game APKs. `SampleGamesTest` guards them.

## Test results

- Host C++: **70 checks, 0 failures** (physics now 12 incl. 3 raycast tests).
- JVM: **94 tests, 0 failures** (new: AiClientTest 7, AiActionApplierTest 6,
  AutoTilerTest 5, UiAndFollowTest 6, SampleGamesTest 3).
- Builds: `:app:assembleDebug` OK; all three sample game APKs OK.

## Known limitations / next steps

- AI replies are applied as one batch; a conversational multi-turn mode with
  diff preview would be the next step.
- Blob auto-tiling assumes the standard 16-tile layout; terrain-rule editing
  (custom mask → tile mapping) is a future increment.
- 2D lighting / normal maps, joints: next engine milestones.
- 3D remains Phase 6 by design.
