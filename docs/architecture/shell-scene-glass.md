# Live analog popup glass

The shell provides the popup outline, tint and controls. The compositor
provides a paint-only backdrop from the layers below the shell. Do not
blur the controls, resample the wallpaper in place of an app, or send
pixels through desktopd.

## Supported surfaces

Live scene glass covers the App Grid in a Node and Notification Log in
Home or a Node. Home App Grid keeps its wallpaper glass. The compositor
advertises the supported popup slots to the watched panel. When it does
not advertise a slot, that popup uses a translucent tint over the live
scene and does not request GPU work. Use `MOTHERSHIP_EXPERIMENT_GRID_FROST=0`
on both processes to restore the old wallpaper-sampled backdrop for
diagnostics. Notification Log items add a local tint to the same glass;
they do not sample the wallpaper again.

The shell reports the popup's logical bounds and current opacity to the
WM. The request uses a fixed slot for each popup and is available only to
the watched shell client. Zero opacity or an unmapped popup disables the
slot. The effect does not own keyboard focus, pointer hits, or layout.
The renderer samples the current background, wallpaper and visible app
branches in scene order. It excludes the shell, the front of the popup and
other popup effects. A Map Settings effect needs a different source boundary
that includes the Center Map display.

The compositor draws the region into a small GPU target with a source
margin for the blur taps. The visible result is at most 304 × 192 pixels;
it uses no full-output copy or CPU readback. The separable Gaussian uses
seven linearly filtered samples per direction, covering thirteen adjacent
texels with no skipped columns or rows. An idle Group uses the blurred
backdrop at full strength under its existing tint; the sharp source must
not show through a second time. The client holes remain unblurred. It reuses the result only
while the popup is open and participating source buffers and geometry
remain unchanged. The signature covers source nodes that intersect the
visible region or its sampling margin. A state change on an intersecting
surface conservatively invalidates the cache even if that surface reports
damage elsewhere. Closing releases the targets.

With explicit sync, wait on each source acquire point and register the
capture pass's completion on every participating source's release merger
before queuing any client-buffer read. Keep the result on the GPU. Render
tint only during a window drag or resize, and schedule a fresh capture on
release. Unsupported inputs or a failed fence leave tint, never a stale
blur. The prototype restores the blur immediately after capture; it does
not yet add a post-drag fade.

## Review

In a disposable session, place two visibly different app windows and
wallpaper under one popup. Change app content without closing the popup:
the blurred region must follow the visible layers and the labels must
stay sharp. Test resize, Escape, outside click, and switching between
both popup types. Try the GTK default renderer and Cairo with the WM
still using GLES. Compare open and closed frame traces at 4K/150% on the
same fixture. For Group, also put a sharp two-tone edge under the header
and gutter. The edge must stay sharp outside the group, blend continuously
inside the glass, and leave app pixels unchanged. A private nested trace is
not native DRM acceptance.

### Private 4K prototype check

On RTX 4060, at a 3840 × 2160 headless output with scale 150% and
explicit sync on, both actual popups reused their cached textures on a
static scene. A high-contrast change inside the halo altered the sampled
edge; another change outside reused the cache. Both refreshed when the
EGL test app submitted a changed frame. With 30 synthetic drag/settle
cycles per popup in each run, cache-hit check p95 stayed at or below
0.004 ms. Release-to-submit p95 was 0.79–0.92 ms for App Grid and
1.08–11.42 ms for Notification Log. The worst sample was 13.43 ms.
This is compositor timing on a disposable headless output. It is not a
physical-display time-to-visible measurement, a video wallpaper check,
or a result from the user's desktop.
