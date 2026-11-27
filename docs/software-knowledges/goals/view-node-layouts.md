# Enter Nodes from Overview and choose a layout

Tracking: [GitHub issue #9](https://github.com/hieutran21198/mothership-wm/issues/9).

| Criterion | Observable outcome | Flow |
| --- | --- | --- |
| G-VIEW-1 | Overview shows actual Nodes at stable session positions, not the Home/Inbox list. Enter or leave a Node without changing any membership. | [F-VIEW-1](../flows/view-node-layouts.md#f-view-1) |
| G-VIEW-2 | Flow defaults to one oriented strip with native window frames. Scroll and local focus changes do not resize apps. An individual window may float, maximize/restore, or close; explicit legacy layout modes remain compatible. | [F-VIEW-2](../flows/view-node-layouts.md#f-view-2) |
| G-VIEW-3 | Layout is remembered per Node for this session. Overview and an empty Node deliver no input to hidden apps. | [F-VIEW-3](../flows/view-node-layouts.md#f-view-3) |
| G-VIEW-4 | A named Node or Default Node can switch between Flow and Stage while keeping its windows and groups. Stage presents a single window or group. Presenting windows stay available in the preview tray and Alt+Tab. | [F-VIEW-4](../flows/view-node-layouts.md#f-view-4), [F-VIEW-5](../flows/view-node-layouts.md#f-view-5) |
| G-VIEW-6 | A keyboard shortcut swaps adjacent standalone Flow windows or neighboring windows within one group on the desktop and in the preview tray. Each window keeps its group membership. Focus follows the window, and the Node keeps the new order for the session. | [F-VIEW-6](../flows/view-node-layouts.md#f-view-6) |
| G-VIEW-7 | Switch directly to a recent Node with Meta + Tab. Browse other Nodes without leaving the current view, then commit or cancel from the keyboard. | [F-VIEW-7](../flows/view-node-layouts.md#f-view-7) |
| G-VIEW-8 | Resize a standalone window in Flow or Stage. See the proposed size while dragging and update the app when the drag commits. | [F-VIEW-8](../flows/view-node-layouts.md#f-view-8) |

No node placement editor, layout persistence across sessions, or changes to Home.
