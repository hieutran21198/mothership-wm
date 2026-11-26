# Center Map visual references

These are the original approved visual files. They inform appearance; the
[current Center Map UI spec](../center-map-ui.md) owns behavior and any later
overrides. Do not use runtime screenshots as concept source.

| Original | Use it for |
| --- | --- |
| [Node Detail concept](mothership-node-quick-profile-rotated.png) | Card geometry: opaque navy, two chamfered contours with solid corners and dashed straights, restrained typography, icon-only spacecraft Connect. |
| [Native GTK Detail preview](mothership-node-detail-gtk-preview.png) | Toolkit study for the card surface, padding, stroke rhythm, type, and spacecraft action. It uses dummy data, not the native app state. |
| [Detail and minimap preview](mothership-gtk-detail-minimap-preview.png) | Minimap appearance: small flat navy inset, equal lower/right clearances, paired contours, viewport rectangle, and diamonds. Do not cut the Map behind it or draw a grid inside it. |
| [Fleet markers](Mothership-fleet-markers-draft.png) | Current top-down broad dual-engine Mothership and narrower single-engine Node. No idle rim. Supersedes the old outline/rim and main Map diamonds, not minimap Node diamonds or relay glyphs. |
| [Fleet arcade Q/E draft](Mothership-Fleet-Arcade-QE-draft.png) | Embedded arcade visual: bronze enemy formation, three cyan ships, dual wing shots and single violet center plasma. Illustrative callouts are excluded. |
| [Space Ops arcade draft](Mothership-Space-Ops-draft.png) | Current arcade art and orientation: suited animal faces, entry chevrons on all four edges and reticle. The target profile, movement arrow and fake counters are concept annotations. |
| [Connect relay storyboard](Mothership-Node-Display-Relay.gif) | Signal and monitor glyphs and the solid route returning after acknowledgment. Its original Mothership icon is superseded by the fleet sheet. |

Use the originals for comparison, not for fixture data. Agent, bell and audio
badges, fake stats and the illustrated app contents are not live features. Node Detail uses real desktopd data on explicit selection.
The current spec also removes the
Node-to-name leader, puts the ship in projected map space with the
“Mothership” label, and makes the minimap a part of Center Map, not Command
Center. The cyan junction accents apply to the main grid only; do not add them
to the minimap.

## Reviewer checklist

Use the [Center Map visual invariants](../center-map-ui.md#visual-invariants)
and the [render runbook](../../../runbooks/render-performance.md). Record the
build revision, renderer (native Wayland or GTK fallback), window size, output
scale, Map zoom, theme and animation setting for each capture. Open the
reference pixels alongside the real build at a matching state. Save a full
view and a readable crop; use samples during effects, not just a final still.

| Start and trigger | Pass when | Capture or observe |
| --- | --- | --- |
| Blank Home, Linear animation; open Map, close with M, reopen. Repeat on the other renderer. | The opening frame hands off once to the settled two 45° contours. No stale opening, doubled frame, or later disappearance. | The same corner in opening, early scan, late scan and settled frames, first open and reopen. |
| At 840 and 960 px dock widths, open Map at 100% and 150%. Hold keys, scroll until the limit, and hold/release primary on blank Map. Repeat over a Node, the minimap and after leaving focus. | Painted bottom is 16 logical px above the unchanged dock; mini and guide stay inside the new corners. Guide lights only eligible inputs and clears without catching them; no pulses in the gap or at the zoom limit. | Show full view, measure the painted edge crops, and compare idle/held/cleared frames with no input or camera shortcut skipped. |
| Open Map from blank Home with M, then close/reopen. Select an unnumbered Node and press Enter; Disconnect. | No Overview selector or Node card appears during opening/radar or on return. Clicking a Node shows its real info in one card above world layers. Enter and the card action use the same acknowledged connection and Disconnect restores camera. | Record frame sequence from the M press through settled state, and capture explicit selection/restore plus actual unnumbered connection. Verify the card follows pan, flips near an edge, and does not pass clicks through. |
| Open Map with minimap visible; let the radar reach its x-position. | The grid above or next to it sweeps. The minimap face, rim and dashes do not light up. It hides when Map closes, irrespective of Command Center. | Matching before/during/after crop that includes minimap **and** visibly swept grid, on both renderers. |
| At rest, then at a second zoom; inspect Mothership and idle/focused Nodes over bright and dark wallpaper. Select a target on a diagonal route. | Wide dual-engine and narrower single-engine silhouettes remain distinct; focus adds a bright outer contour without changing label size or baseline. Both route ends have positive painted clearance. The simplified ship and Node diamonds stay world-anchored in the minimap while its viewport rect moves. | Same-scale crops at 100% and 150%, at rest and after pan/zoom, with measured contours and gaps. Include the minimap and a custom SVG if configured. |
| Pan both ways with short numbered labels. Bring one label to the inside edge, then pan it back. | Names stay one line and full until truly clipped, then return in full. No new ellipses, wrap, or changing size. | Before, after and right at the edge with the same zoom and font. |
| Select a Node with motion enabled, wait more than two periods, pan/zoom, change focus, connect, and repeat with Off. | Only the dashes loop toward the Node, with unchanged endpoint gaps. Pan/zoom does not restart the phase. Focus departure pauses; return resumes. Deselect, close or Connect removes the dash tick; Off never starts it. | Native frames in time order, endpoint gaps, tick lifetime, and confirmation that the grid/minimap did not redraw each tick. |
| Open native Command Center, Settings and a Node label containing Vietnamese; then try an explicit Map font override. | The bundled face resolves for owned text and Vietnamese glyphs at 100% and 150%, without collisions. Only Map labels follow the explicit override. External applications keep their fonts. | Actual family and fallback check for each surface, plus full-size crops with accents and compact controls. |
| From Home, slash, App Grid and active Map, try G. In the game press Q, E twice, hold a direction and leave focus, return, then Escape. | Only Map enters. Q paints six wing paths; E paints one central beam and respects cooldown. The frame/dock stay fixed. No held motion on re-entry; Escape restores camera/selection. | Capture comparable 100% and 150% game/Map pixels, pause frames, both weapons, and the restored route; exercise input without artificial focus on the host. |
| In Space Ops, bring the pointer through left, right and rear quadrants while using Q/E and mouse buttons, then leave the frame/focus. | All ships face the reticle, shots leave successive paired wing mounts and only Mothership emits plasma. Leaving the field stops mouse fire without canceling held Q; focus loss stops motion and clears both sources. Warned spawns arrive from four sides; egg slip, mud slow and the returning bone are distinct. | Real nested protocol events, frame samples on both sides, scoped pause and Map restore. Compare a 4K session maximized during play to the same scene opened at 4K. |
| In Map View, press blank interior, move below threshold, then pull in both directions. Repeat zoomed, after leaving the frame or outer focus during drag, and from a Node/name and the minimap. | Motion below threshold does not pan; a click still selects or dismisses. Empty-space drag moves projected markers 1:1 until release/bounds; no drift or click-through. Protected surfaces do not start a drag. Fresh press works after loss and re-entry. | Native pointer sequence, marker displacement in logical px at both zooms, unchanged grid/frame/wallpaper and ordinary click/Connect results. |
| While Map is open, hold W and leave outer-window focus. Return, then press fresh W, use wheel, and press M. | No movement while unfocused or on re-entry without input. Fresh W and wheel resume without replay; M closes normally. | Record the visible Map/pose across leave/re-entry, fresh input and close. State whether it was actual COSMIC or a native isolated parent. |

Pass only with the listed evidence. If a case cannot be exercised, mark it
**not verified**; green code tests do not establish a visual match. List any
mismatch and distinguish approved later overrides and dummy-only data.

Original byte checksums (SHA-256):

- `Mothership-Space-Ops-draft.png`: `d9948b2687aba672645264df3b34f08a591164698e4549a826f53bee0924ef12`
- `Mothership-Fleet-Arcade-QE-draft.png`: `2e9bc185f46cd9ffd28ccdb5fa7743b8c47133bb78e4722b318fbda905a36275`
- `Mothership-fleet-markers-draft.png`: `56b762fa89faf16be82e238be03bab7f45a33f93c37dfe6d20758d91ac13347c`

- `mothership-node-quick-profile-rotated.png`: `d93d9d080c82b9db6ae771c3a846befe020d2cb39f992c999364c4dda0e78d75`
- `mothership-gtk-detail-minimap-preview.png`: `cd867252d12bdd4ad064b3fffc5f4f816a80c6e767740031f4b5adfab847c54c`
- `Mothership-Node-Display-Relay.gif`: `8d53892ad47bc31118261e945d37338216432d1e42b75363cbb540a514f3aece`
- `mothership-node-detail-gtk-preview.png`: `2f22cd3f654c369a6afd1b8f7b792d56a784eccdf1e889418740b5a54ad79a06`
