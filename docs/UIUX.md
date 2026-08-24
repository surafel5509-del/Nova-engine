# UI/UX + Tools Improvement Report

Milestone: fix the responsive-layout problem (your screenshot), enlarge
typography, give every tool an icon, and expand the editor with a professional
timeline, a complete file manager, 3D physics, and a lighting/world system.

## Responsive layout fix (the main complaint)

- Editor now uses `BoxWithConstraints` + flexible weights instead of fixed
  pixel heights: compact mode splits the space as 55% viewport / 45% panels,
  so **nothing is cut off when the screen rotates or the window resizes**.
- Wide (desktop) mode keeps hierarchy / viewport / inspector as before.
- Layout honors the Settings desktop/mobile mode AND auto-detects orientation.

## Typography + icons

- **Larger readable type scale** (Material `Typography` override: body 15–17sp,
  titles 16–22sp) across the whole app.
- **Every tool has an icon**: the toolbar is now an icon palette of 19 tools
  (Select ➤, Move ✥, Rotate ⟳, Scale ⤢, Rect ▭, Pan ✋, Zoom 🔍, Tile ▦,
  Brush 🖌, Eraser 🧽, Picker 💧, Sprite 🖼, Animation 🎞, UI 🔲, Particles ✨,
  Physics ⚛, Camera 🎥, Audio 🔊, Light 💡, Script 📜).
- **Rotate / Scale / Rect tools are now functional** in the viewport (drag to
  rotate, drag to scale, rect = move+scale), with undo via the drag snapshot.

## Professional animation timeline

- Playback controls (⏮ ⏴ ▶ ⏸ ⏹ ⏵), loop toggle, live playhead time.
- Per-track keyframe markers on a time ruler; tap a keyframe for a menu:
  **move ±0.5s, copy, paste, delete**.
- "+ Track" (x, y, rotation, scaleX, scaleY) seeded from the entity's current
  values; add keyframe at the playhead.
- Tracks still sample natively during Play.

## Complete file manager

- Search/filter box, sort by name/size, **Copy / Paste / Duplicate**,
  **Export ZIP**, plus the existing create/rename/move/delete.
- **Preview dialog** for files: images render, text/scripts preview inline.
- ZIP import retained with the hardened traversal guard.

## 3D physics world (real)

- `engine/physics/PhysicsWorld3D.{h,cpp}` — gravity, integration, sphere/
  sphere + sphere/ground collisions, restitution, inverse-mass separation.
  Host-tested (6 checks). `PhysicsBody3DComponent` in the Kotlin model.

## Lighting + World/Sky

- `LightComponent` gains **intensity** and **type** (directional/point/spot/
  sun/ambient); the renderer scales light color by intensity.
- `WorldEnvironmentComponent` (sky color, horizon, fog color/density, ambient
  intensity) — the 3D renderer clears to the sky color and scales ambient.

## Test results

- Host C++: **95 checks, 0 failures** (physics3d 6 new).
- JVM: **110 tests, 0 failures**.
- Builds: `:app:assembleDebug` green (3-ABI engine).

## Honest scope notes

This pass delivered the layout fix, typography, icon tools, real Rotate/Scale/
Rect, the professional timeline, the full file manager, 3D physics, and the
lighting/world model. The reference image's full 3D feature set (terrain
sculpting, sun/sky managers, sculpting brushes, 3D UI builder, minimap,
advanced camera rigs, full APK-signing workflow) is a much larger body of
work — those remain as explicit next milestones, not placeholders.
