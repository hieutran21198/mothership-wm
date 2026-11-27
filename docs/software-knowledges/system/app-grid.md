# App Grid

**A** opens the installed-app picker only in focused blank Home. The dock
stays visible and the picker does not overlap the [Center Map](center-map-ui.md)
or [slash Node search](home-panel.md). Escape or a click outside closes it on Home and in a Node. The outside
click does not activate the underlying app or dock. **A** stays a movement
key in Map or focused App Grid results, and a letter in a text field or application.

Fade the centered picker in over 180 ms and out over 140 ms with gentle
ease-in-out. Do not slide, scale, or move the panel. Release App Grid input
at the start of the exit. Preserve the outside dismiss click without passing
it to the app below. Fast reversal starts at the current opacity. Skip the
fade when system animations are off or a different shell view takes over.

Use the [approved layout](app-grid-reference.png) with the newer analog
material. Use one centered cool frosted overlay with thin, separated 45°
corner strokes, lightly tinted tiles, no icon wells, a search
field, a compact Map-style keyboard guide, and no title or Escape badge. Show
up to 20 tiles per page, arranged in four rows of five at standard size. Fit
fewer columns or rows on narrow or short outputs and never wrap a
tile name. Ellipsis does not replace the full name available on hover or
keyboard focus. Use the installed desktop entry's own icon and current icon
theme, with a neutral window glyph only if the declared icon cannot resolve. The reference glyphs are illustrative, not
a command to replace or recolor actual app artwork.

- Follow the [App Grid flow](../flows/use-app-grid.md) for paging, search and
  focus. The analog **Previous** and **Next** buttons flank a page counter
  labeled **Pinned**, **All Apps** or **Search**. Disable unavailable
  directions. Supported standalone launches stay first in All Apps.
- Search matches apps by desktop name or ID. **W / A / S / D** and the arrow
  keys move among tiles. Horizontal movement stays in the row except at the
  first and last tile of a page. **W** or **Up** from the first row and
  **Backspace** from any tile return to Search without changing the query.
  Search retains normal typing, caret movement and Space. The guide shows
  **↓ Results** in Search and **W / A / S / D** and **Backspace** in the results.
  Show **Enter** and **Esc** in both states. Do not add Launch, Close, or Search captions.
- Reveal the separate pin only on hover or tile keyboard focus. Clicking it
  or pressing Space on the tile toggles the pin without launching. Persist
  exact desktop-file IDs in `app_grid.pinned` under
  `~/config/.mothership/config.json`, leaving other modules intact.
  Toggling a pin in All Apps or Search keeps that app focused. In Pinned,
  removing a pin focuses the next tile or the last surviving pin. If the last
  installed pin is removed, show the app in All Apps.
- The catalog and launch policy are separate. Do not activate apps through the host D-Bus session. In nested mode,
  the supported standalone paths are Ghostty, COSMIC Terminal, COSMIC Files, Ark, Brave
  and btop.
  They use fixed commands against the nested Wayland socket, not the
  desktop entry's activation or arguments. Ghostty explicitly disables
  single-instance, including on systems where its `.desktop` opts into
  D-Bus. Ark uses Qt Wayland. Brave opens a blank tab with Wayland and a
  private profile under the current nested session runtime, which is
  removed when that session ends; it does not reuse the host profile or
  disable Chromium sandboxing. btop is the fixed command launched inside
  a separate nested Ghostty terminal. Other entries name the unavailable app
  without trying the host. The WM is the source for whether a new window
  mapped; a child exiting is not a successful launch.
  In Mothership Dev (native), visible desktop entries with standalone
  graphical `Exec` commands may also launch through GIO. Use the current
  Mothership display and the private notification bus. Do not use this
  fallback for terminal entries or entries that request D-Bus activation.
  X11-only apps still require XWayland, which the native session does not
  provide. This launch environment does not isolate app files or profiles.
  A uniquely matched new window joins the Node that requested it. A
  new window takes keyboard focus the first time it appears in the shown
  view; one that lands in a hidden Node or Inbox does not. A
  launch marker follows a new process through wrappers, even when it
  detaches. Never move an older window or match by app name alone. An
  existing instance that does not carry the marker needs an explicit
  adapter, or its new window stays in Default / Inbox. Home
  launches bring the Default Node forward when enabled.
- An absent or uninstalled pin is not shown. No control rearranges apps
  outside Mothership, changes the host desktop, or accepts arbitrary
  commands from the search field.

Review at 100% and 150% scale and one narrow output. Verify boundaries,
thumbnail colors, full and truncated names, tile cap, both pager buttons,
hover/focus pin, Down into results, and WASD and arrow navigation. Return to
Search with a held W or Backspace and check the query stays intact. Verify
Space in input versus on a result, Escape, restart pin persistence, Map **A**,
and actual native nested mapping for a supported app. For an unsupported
entry verify no application or desktop activation occurs.
