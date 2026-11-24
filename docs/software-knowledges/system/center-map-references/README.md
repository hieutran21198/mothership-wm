# Center Map visual references

These are the original approved visual files. They inform appearance; the
[current Center Map UI spec](../center-map-ui.md) owns behavior and any later
overrides. Do not use runtime screenshots as concept source.

| Original | Use it for |
| --- | --- |
| [Node Detail concept](mothership-node-quick-profile-rotated.png) | Opaque navy Detail, two chamfered contours with solid corners and dashed straights, restrained typography, icon-only spacecraft Connect. |
| [Native GTK Detail preview](mothership-node-detail-gtk-preview.png) | Closer toolkit reference for the card surface, padding, stroke rhythm, type, and spacecraft action. It uses dummy data, not the native app state. |
| [Detail and minimap preview](mothership-gtk-detail-minimap-preview.png) | Minimap appearance: small flat navy inset, equal lower/right clearances, paired contours, viewport rectangle, and diamonds. Do not cut the Map behind it or draw a grid inside it. |
| [Connect relay storyboard](Mothership-Node-Display-Relay.gif) | Mothership marker: upright cyan ship outline with swept trailing notches and a spine inside a thin muted rim. Also shows the signal and monitor glyphs and the solid route returning after acknowledgment. See frame 0 for the ship shape. |

Use the originals for comparison, not for fixture data. Agent, bell and audio
badges, fake stats and the illustrated app contents are not part of the
live Detail baseline. The current spec removes the
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
| Open Map with minimap visible; let the radar reach its x-position. | The grid above or next to it sweeps. The minimap face, rim and dashes do not light up. It hides when Map closes, irrespective of Command Center. | Matching before/during/after crop that includes minimap **and** visibly swept grid, on both renderers. |
| Select a Node whose Detail covers a second Node or the connecting route. Click the covered marker. | Card is opaque, world does not show through, and click does not select the covered Node. The card's right-hand preference and fallback fit the viewport. | Crop the actual overlap and record selection before/after the click. |
| Inspect Detail before and after the same Node's first acknowledged Connect. | Running time has a clock; Last active has a pulse. Both share a single baseline with a separator only when both exist. Missing values leave no placeholders or extra row. | Native crops of the same card with one and both real values. |
| At rest, then at a second zoom; inspect Mothership and idle/focused Nodes over bright and dark wallpaper. Select a target on a diagonal route. | Base contours match extent, focus adds a distinct ring, idle remains readable and hollow, label size/baseline match, and the route has a positive painted gap at both ends. | Same-scale crops with measured contours and gaps; show dashed and returning-solid geometry if changed. |
| Pan both ways with short numbered labels. Bring one label to the inside edge, then pan it back. | Names stay one line and full until truly clipped, then return in full. No new ellipses, wrap, or changing size. | Before, after and right at the edge with the same zoom and font. |
| Select a Node with motion enabled, wait more than two periods, pan/zoom, change focus, connect, and repeat with Off. | Only the dashes loop toward the Node, with unchanged endpoint gaps. Pan/zoom does not restart the phase. Focus departure pauses; return resumes. Deselect, close or Connect removes the dash tick; Off never starts it. | Native frames in time order, endpoint gaps, tick lifetime, and confirmation that the grid/minimap did not redraw each tick. |
| Open native Command Center, Settings and a Detail containing Vietnamese; then try an explicit Map font override. | The bundled face resolves for owned text and Vietnamese glyphs at 100% and 150%, without collisions. Only Map labels follow the explicit override. External applications keep their fonts. | Actual family and fallback check for each surface, plus full-size crops with accents and compact controls. |
| In Map View, press blank interior, move below threshold, then pull in both directions. Repeat zoomed, after leaving the frame or outer focus during drag, and from a Node/name, Detail/Connect and the minimap. | Motion below threshold does not pan; a click still selects or dismisses. Empty-space drag moves projected markers 1:1 until release/bounds; no drift or click-through. Protected surfaces do not start a drag. Fresh press works after loss and re-entry. | Native pointer sequence, marker displacement in logical px at both zooms, unchanged grid/frame/wallpaper and ordinary click/Connect results. |
| While Map is open, hold W and leave outer-window focus. Return, then press fresh W, use wheel, and press M. | No movement while unfocused or on re-entry without input. Fresh W and wheel resume without replay; M closes normally. | Record the visible Map/pose across leave/re-entry, fresh input and close. State whether it was actual COSMIC or a native isolated parent. |

Pass only with the listed evidence. If a case cannot be exercised, mark it
**not verified**; green code tests do not establish a visual match. List any
mismatch and distinguish approved later overrides and dummy-only data.

Original byte checksums (SHA-256):

- `mothership-node-quick-profile-rotated.png`: `d93d9d080c82b9db6ae771c3a846befe020d2cb39f992c999364c4dda0e78d75`
- `mothership-gtk-detail-minimap-preview.png`: `cd867252d12bdd4ad064b3fffc5f4f816a80c6e767740031f4b5adfab847c54c`
- `Mothership-Node-Display-Relay.gif`: `8d53892ad47bc31118261e945d37338216432d1e42b75363cbb540a514f3aece`
- `mothership-node-detail-gtk-preview.png`: `2f22cd3f654c369a6afd1b8f7b792d56a784eccdf1e889418740b5a54ad79a06`
