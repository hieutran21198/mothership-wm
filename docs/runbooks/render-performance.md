# Render performance runbook

Measure, make the smallest fix, and remeasure in one assignment. Do the
checks yourself. Bound the work to what changed without changing the
intended timing, visuals or input behavior.

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
