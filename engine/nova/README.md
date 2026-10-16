# The Nova engine (`engine/nova`)

A from-scratch engine in C++17 with no third-party dependencies — no zlib, no
SDL, no GL loader, no vendored library. Everything the engine needs it either
writes itself or does from the C++ standard library.

It builds with a single `make` on a host with nothing but GCC or Clang and a
POSIX socket library. There is no CMake, no package manager and no network
fetch step.

```
make            # tests + both binaries
make test       # 247 tests, 28 suites
make player     # build/host/bin/nova_player
make server     # build/host/bin/nova_server
make asan       # the same tests under -fsanitize=address,undefined
```

## What is in here

| Directory | What it holds |
|---|---|
| `base/` | timing, logging, JSON, deterministic RNG, filesystem, in-tree DEFLATE |
| `math/` | vectors, 2x2/3x3/4x4 matrices, quaternions, AABBs, frustum extraction |
| `ecs/` | sparse-set entity/component world with change-filtered queries |
| `scheduler/` | fixed-timestep accumulator with an explicit spiral-of-death clamp |
| `scene/` | node hierarchy, local/world transforms, parent and child ordering |
| `physics/` | the 2D world: SAT narrowphase, sequential impulses, sleeping, CCD |
| `render/` | RGBA8 images, PNG codec, software rasterizer, cameras, meshes, renderers |
| `input/` | frame-boundary input state and an action map |
| `net/` | HTTP server, WebSocket, SHA-1, base64 |
| `app/` | the runtime (`Player`), its host (`FrameHost`), and both CLIs |
| `tests/` | one suite per subsystem, all wired into `nova_tests` |

## Running it

Headless, writing PNG frames:

```
./build/host/bin/nova_player --frames 120 --width 480 --height 270 --out /tmp/frames
```

Serving the editor, which is a browser front end talking HTTP + WebSocket to the
same process:

```
./build/host/bin/nova_server --port 8090 --web web
```

Open `http://127.0.0.1:8090/`. The viewport shows the engine rendering live, the
hierarchy and inspector read the scene graph, and the arrow keys drive the
pusher. `nova_player --serve` serves the same thing without the editor's own
routes.

## The HTTP and WebSocket surface

| Route | Answers |
|---|---|
| `GET /api/status` | engine name, framebuffer size, running state, action map |
| `GET /api/telemetry` | per-body positions, velocities, contacts, frame times |
| `GET /api/scene` | the scene graph, as a nested node list |
| `GET /api/actions` | every action with its live pressed/held/released state |
| `GET /api/host` | host counters, client count, document root |
| `GET /api/frame.png` | the most recent encoded frame |
| `POST /api/command` | a text command (`pause`, `resume`, `reset`, `spawn n`, …) |
| `WS /ws` | PNG frames as binary messages, telemetry as text, input in |

All of these answer from a snapshot the loop thread publishes, so an editor
panel can never stall the simulation.

## Notes on the parts that are easy to get wrong

**Physics.** A circle's lever arm is derived from the contact normal
(`normal * radius`), not from a contact point that drifts as the bodies move.
Using the point injects energy into every rolling body: a ball dropped at 0.14 u/s
came back a frame later at 0.145. With the normal-derived arm the rolling
relation holds to five digits, and a sliding 2D disc comes to rest at exactly
`2/3 v0` with `v/r` spin — the analytic result, not an approximation.

**Rolling resistance** is optional and off by default (`World::rollingFriction`);
the demo scene sets `0.35`.

**Tunnelling is prevented, not clamped.** A 400 u/s circle meets a 0.1-thick wall
and stops at the wall, with a real time-of-impact and contact normal from
conservative advancement. Clamping the position would put the body somewhere it
never was.

**The rasterizer** steps its edge functions per pixel, hoists the perspective
divide out of any triangle whose three `1/w` values are exactly equal (any
orthographic projection — and bit-identical, because multiplying by `1/w` when
`w == 1` is exact), skips depth interpolation when nothing will read the depth
buffer, and dispatches its blend mode and texture filter once per triangle
instead of once per pixel. Together those took a full-screen textured quad at
960x540 from about 24 ns per fragment to about 20 ns, and a whole 960x540 frame
of the demo scene (about two screens of overdraw) from about 50 ms to about
37 ms. The remaining cost is the fragment shader itself, which is
latency-bound: the per-pixel chain from edge function to stored byte is long
enough that the loop runs at roughly one instruction per cycle no matter how
much of it is hoisted. Making that materially faster means vectorizing the
fragment path, which is not done yet.

**The host does not render frames nobody will see.** It simulates at the target
rate but rasterizes only when a WebSocket watcher is attached and the stream is
due, or when someone requests `/api/frame.png`. A paused game stops encoding a
picture that cannot have changed. An idle host sits at about 1% of a core.

## What is verified, and what is not

Verified on this host, by the commands above:

- `make test` — 247 tests, 28 suites, 51053 checks, all passing.
- `make asan` — the same 247 tests under ASan, UBSan and LeakSanitizer, clean.
- `node tools/browser-check.js http://127.0.0.1:8090` — every browser-contract
  check passes against a running host: static files, REST, the RFC 6455 upgrade,
  real PNG frames at the configured size, commands over both transports, and a
  held key actually moving the character.

Not verified, because this host cannot build it: the Kotlin/Compose Android app
under `app/` and the legacy GLES/JNI engine under `engine/` (everything outside
`nova/`) need a JDK, Gradle, the Android SDK and NDK, none of which are
installed. They are written but unproven here, and no claim is made about them.