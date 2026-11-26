# Render performance runbook

Measure, make the smallest fix, and remeasure in one assignment. Do the
checks yourself. Bound the work to what changed without changing the
intended timing, visuals or input behavior.

## Capture a live Mothership session

Press **Win + Shift + F12** in the affected app to start recording. Repeat
the problem, then press the same chord to stop and save. There is no
redraw tint, screen recording, focus change or terminal step. The WM
writes `render-debug-*.csv` under `$XDG_STATE_HOME/mothership-wm/render-debug`
(default `~/.local/state`). A safety limit stops recording after 60 seconds;
the header reports samples dropped if the bounded buffer fills.

For a timed capture with optional NVIDIA device metrics instead:
Run `task mothership:render-debug` from the repository in a terminal inside
the target session. Reproduce the problem during the next 10 seconds.
There is no HUD or extra redraw. A private local endpoint starts capture;
repeated requests during a capture do not reset it. The command saves the
CSV under `$XDG_STATE_HOME/mothership-wm/render-debug` (default `~/.local/state`).
On an NVIDIA system it also samples GPU load, memory and decoder load
separately when `nvidia-smi` is available. The graphics statistics are
device-wide, not timing for individual commands.

For a separate GPU-timing check on the pinned GLES2 renderer, use the exact
current session's private Render Debug FIFO. With its path in `fifo`:

```sh
timeout 2 sh -c 'printf "capture gpu\n" > "$1"' _ "$fifo"
```

This starts a 10-second capture and logs where the CSV was saved in the
session's runtime directory. Run a normal capture for the same action
first. The optional scene query adds work; do not use it as the baseline
for presentation pacing.

- `present` / `discard` are output-backend results. Join with `commit` using
  the commit sequence. Compare consecutive presentation timestamps using
  the same output and backend. Only DRM results with the hardware flags
  are evidence of hardware presentation. Nested and headless are controls.
- `commit` records the submitted buffer size and the changed region in
  buffer pixels: actual region area, rectangle count and bounding box.
  Damage area `-1` means unavailable. This is not a GPU fill-rate measure.
- `surface` records root client/layer damage: declared logical and buffer
  pixels separately, effective logical damage in `damage_px`, and only a
  session-scoped window ID if applicable. No names or pixels are recorded.
  Compare surface rows in the same interval with the output commit. The
  root-only probe does not cover every subsurface. For surface and commit
  rows, `damage_regions` stores up to eight rectangles as
  `x1:y1:x2:y2` separated by `|`. `rects` remains the full count.
- `precommit` marks the output backend boundary. In the same commit
  sequence, subtract it from `commit` to isolate CPU time spent on that
  phase. This is not GPU execution time.
- The source mask identifies activity since the previous frame callback:
  app (1), panel (2), wallpaper (4), Map HUD (8), WM motion (16), frost
  rebuild (32). A source in the same interval is a lead, not proof of
  causation. A zero mask can include cursor activity or untracked changes.
- `frame` records CPU wall time before and during the scene submit,
  not GPU execution time. Frost states are off (0), cache hit (1),
  rebuild (2), or fallback (3). A rebuild runs the frost passes; it is
  not a measurement of a texture upload. This capture does not read
  pixels, app contents or titles.
- `key_events`, `focus_changes`, and `configures` count forwarded key
  events with a focused surface, Wayland keyboard-focus changes, and app
  xdg configure emissions since the preceding frame row. No key identity,
  text, direction, or per-key timestamp is recorded.
- On `surface` and `commit` rows, `buffer_origin` is none (0),
  shared memory (1), DMA-BUF (2), or unknown (3). It reads buffer metadata
  without mapping storage. This classification does not identify an app's
  renderer or establish its actual copy or fence cost.
- Optional `gpu` rows join with `commit` on `seq`. The capture samples
  every fourth drawable frame when no query is pending. After 50 ms it
  checks result availability; at most three polls, never a fence wait.
  `scene_pre_render_ns` is wlroots setup before its render pass.
  `render_pass_ns` is the renderer's GPU-inclusive time span, not pure GPU
  execution or fence wait. `gpu_probe_ns` is CPU wall time spent polling.
  `gpu_status`: ready (1), unavailable (2), disjoint/not ready (3),
  incompatible clock (4), or probe exceeded its CPU budget (5).
  An invalid clock, a slow probe, or repeated unavailable results stops
  further GPU queries in this capture. Missing durations are `-1`.
  Compare surface commit to frame start for
  queueing before WM rendering; timing alone does not prove pixel correctness.
- Run matched captures with PNG vs MP4, Clear vs Frosted, on idle and
  after the same interaction. Compare the presence of repeated damage and
  the longest gap while motion should be active. Long gaps while idle
  can be healthy.

## 1. Measure the baseline

- Pick one reproducible action and a pass condition. Match the viewport,
  scale, renderer, settings, scene and output refresh across runs.
  Test the first run after startup (cold) and repeats in the same
  process (warm). Use 100% as a control and test the target scale
  separately. Log the physical buffer size as well as the logical size.
- From the repository root, build with the checked-in tasks:

  ```sh
  devenv shell --offline --profile wm-native -- task wm:build desktopd:build
  devenv shell --offline --profile panel-native -- task panel:build
  devenv shell --offline -- go build -o bin/mothership ./cmd/mothership
  ```

- In an isolated Wayland session, or on the target compositor when safe
  and in scope, launch one owned nested fixture. To trace damage, run:

  ```sh
  run=$(mktemp -d)
  mkdir -m 700 "$run/config"
  XDG_CONFIG_HOME="$run/config" WAYLAND_DEBUG=client ./bin/mothership \
    >"$run/session.out" 2>"$run/wayland.log"
  ```

  Replay the action; for Center Map entry use **Command Center → Map
  View**. Stop only the fixture. In the trace, identify the Panel and
  outer WM surfaces by connection and role. Compare buffer attach,
  damage and commit, not isolated protocol lines. For a moving
  subsurface, inspect the old and new extents and the commit of its
  parent. Confirm that the old position clears.
- If this is a display-pacing task, capture real COSMIC
  `wp_presentation` on the outer WM surface. There is no checked-in
  command that captures it. Use the supported
  [protocol procedure below](#capture-real-presentation) in scratch.
  If you cannot get real feedback, keep the isolated diagnostic
  and mark display pacing **unverified**.

## 2. Fix the stage that wastes work

Change only the stage the trace identifies. If preparing or drawing
unmodified content is the cost, retain it and bound the drawing area.
If submission damage covers more than the changed pixels, check why
before adding a surface. Keep timing and quality fixed so they do not
mask the result. Reuse buffer contents; never overwrite mutable storage
before compositor release. Preserve the fallback path.

## 3. Remeasure and decide

- Repeat the same cases with baseline and candidate. Compare cold and
  warm runs on the same target and check transitions, not just the
  average. For a moving effect, verify the remeasurement includes
  its removal and the first affected frame after it.
- **Pass:** The targeted work decreases and meets the chosen budget.
  Verify equivalent output, no stale pixels on cancel or resize, correct
  input routing, a usable fallback, and no further animation redraw
  once settled. If smooth presentation is a requirement, check the
  longest actual present gap against the target output's frame budget
  during active motion. Do not treat a render callback, Wayland commit,
  synthetic timer or Render FPS as a displayed frame.
- **Fail:** If the fix does not help or causes a regression, remove
  only that change. Try the next small fix only if the trace identifies
  it. Stop when the baseline does not reproduce or you cannot isolate
  the gap. If only the target check is missing, mark it unverified; do not
  call the end-to-end check a pass. Escalate only if completing the
  requested proof needs target access or work outside the authorized scope.

Report the check, change, before/after and remaining gap in the issue
linked from the [owning Goal](../software-knowledges/goals/show-center-map.md).
Keep measurements and captures out of the runbook.

## Reference: choose the next tool

| Need | Use | Boundary |
| --- | --- | --- |
| App update or snapshot | Add temporary per-phase monotonic markers. Use [Sysprof](https://github.com/GNOME/sysprof) CPU samples if stacks are needed and permissions allow. | App work, not GPU time or presentation. |
| Isolate rendering from COSMIC | Use [`gsk_renderer_render_texture()`](https://docs.gtk.org/gsk4/method.Renderer.render_texture.html) on the same renderer, GPU and physical target. Compare the full scene with each layer. Read the actual GL renderer; `nvidia-smi` only identifies and samples the device. | Wall time through the render API, not submission or GPU duration. |
| Measure GPU commands | Use [`GL_EXT_disjoint_timer_query`](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_disjoint_timer_query.txt) when supported. Read results later and discard disjoint samples. | `glFinish()` waits for queued work. Its wall time is not a GPU timer. Keep it out of pacing runs. |
| Check compositing and display | Use [Wayland core](https://wayland.freedesktop.org/docs/html/apa.html) damage/commit and real host feedback below. | [GTK4 invalidates widgets](https://docs.gtk.org/gtk4/migrating-3to4.html#invalidation-handling-has-changed). A small GTK draw does not guarantee a small buffer or output damage region. |

Bring extra tools into a disposable `devenv` with the project's locked
inputs only when needed. Do not change host settings, permissions,
drivers or the host compositor to run these checks.

### Capture real presentation

On an in-scope COSMIC output, attach one
[`wp_presentation` request](https://raw.githubusercontent.com/wayland-mirror/wayland-protocols/main/stable/presentation-time/presentation-time.xml)
before each actual outer buffer commit. Add the probe in scratch, not to
the shipping build. Record commit ID, phase, submit time, size and
scale. Join each result by ID. Use the declared `clock_id` and verify
the target output, refresh and flags. Subtract submit and present times
only on matching clocks. Count displayed frames, discards and
unanswered IDs separately. Include the longest gap across phases.
Keep cold/warm and fallback runs separate.

### Case: Center Map radar

The narrow radar shared a large HUD with a full-size Cairo frame and
grid mask. A small gradient did not prevent full-surface work. The fix
bounds static paint and gives the moving strip its own damage. The
[Center Map architecture](../architecture/center-map-rendering.md)
owns the production behavior and GTK fallback.
