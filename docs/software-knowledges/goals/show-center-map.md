# Show a configurable Center Map HUD

Tracking: [GitHub issue #10](https://github.com/hieutran21198/mothership-wm/issues/10).

**Current default:** A 45° framed Center Map in Overview. The frame, grid,
wallpaper and app geometry stay fixed. WASD pans Node anchors and labels
inside the Map; Connect opens the acknowledged workspace normally, without
moving the map or background.

| Criterion | Observable outcome | Flow |
| --- | --- | --- |
| G-MAP-1 | Show and hide the map repeatedly without changing active view, current Node positions, membership, keyboard focus or pointer routing. | [F-MAP-1](../flows/show-center-map.md#f-map-1) |
| G-MAP-2 | A local theme controls tint, tint opacity, grid, text, label font family and size, border color, and exact-name SVG overrides. Invalid input falls back to usable defaults. | [F-MAP-2](../flows/show-center-map.md#f-map-2) |
| G-MAP-3 | The idle HUD reads without hover. Paint the sparse, clipped grid and one 45° chamfered outline without coordinate ticks or labels, then symbols, then annotations globally. Reuse the Overview's live Nodes and stable slots. | [F-MAP-2](../flows/show-center-map.md#f-map-2) |
| G-MAP-4 | Set border color, outer margin, inner padding and Clear/Frosted separately in Command Center and preview without restarting. Save patches the shared user configuration; Cancel restores the last active values. R reloads external edits from blank Home only; invalid edits leave the running configuration intact. | [F-MAP-3](../flows/show-center-map.md#f-map-3) |
| G-MAP-5 | Open Map View directly from Command Center. WASD and blank-space drag pan Nodes under the fixed grid; wheel zooms icons, not labels, grid or wallpaper. M toggles Map View and H returns to blank Home without reopening or closing apps; Escape only dismisses an open profile. Normal app shortcuts are not mapped to Map controls. | [F-MAP-4](../flows/show-center-map.md#f-map-4) |
| G-MAP-6 | Intentional Node selection opens one quick profile. Outside click dismisses it; choosing Connect separately opens the acknowledged workspace. Show the real Node name, optional description, age, last acknowledged entry and unique current app icons when available. Never show placeholder stats or infer unavailable apps. | [F-MAP-5](../flows/show-center-map.md#f-map-5) |

Minimap, launcher changes and 3D rendering are out of scope.
