# Search windows and connect to one

**Status:** implemented 2026-11-27; owner accepted in a native Mothership Dev session (candidate `build-96C5L99n`) on 2026-11-27.
Supports [Find any open window by name](../goals/find-any-window.md).
Design: [Fleet Ops preview, scene 5](../system/fleet-ops-preview.html).

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Open | Panel | Completing the `windows` module in [Slash](../system/home-panel.md#slash-palette) on Home or with **Win + /** in a Node requests one window list. Unlike Node-scoped commands, it covers the whole session. |
| List | WM → Panel | `window_list` (private shell protocol v27) returns one `window_list_row` per live mapped toplevel: session window ID, title and app ID, then `window_list_end`. No pixels are sent. An older WM shows **Window search needs a newer Mothership WM.** |
| Join | Panel | Join rows with the desktopd snapshot for membership; drop windows desktopd does not list as mapped. A window with no named Domain is labelled **Default** with key 1, and is listed only while the Default Node is enabled. Order by Node recency (the [switcher order](view-node-layouts.md#f-view-7)), unvisited Nodes by name with Default last, then by title. |
| Filter | Panel | Case-insensitive substring match on title, app name and app ID. Prefix matches rank first. Show app icon, one-line title and Node label with its number key. Show **No matching windows** when empty. |
| Connect | Panel → desktopd → WM | Enter sends [Connect to a window](#connect-to-a-window) with the session and window ID. Close Slash after the acknowledged selection. |

## Connect to a window

This is a shared step, also used by the [comms pin](pin-comms-window.md).
The caller sends [`focus.window`](../../architecture/desktopd-client-ipc.md)
with the session and window ID. desktopd resolves the window's current
Domain at execution, under the same lock as the selection, so a moved
window takes the caller to its new Node. Inbox members select the Default
Node, even when it is already shown. The WM receives `node_window` or
`home_node_window` with the target, ranks it first before projecting and
makes it the keyboard target when it is mapped and eligible
([WIN-2](../artifacts/window.md)); a minimized target is restored through
the normal preview focus path. Otherwise it follows the normal focus
restore. A stale session or closed window is rejected with `stale_session`
or `unknown_window`; the view does not change. The WM advertises this with
`ready_window_focus`.

### F-SCAN-1

Open three windows in two Nodes and one in Inbox, with two sharing an app
ID. Type `windows` then part of one title. Matching rows show the right
Node labels; the duplicate app ID windows appear separately by title.
Trace desktopd's socket traffic and panel logs: no title appears.

### F-SCAN-2

From Home, select a window in a hidden Node and press Enter. The Node
opens and the chosen window, not its last focused sibling, receives the
next key. Repeat from inside another Node with **Win + /**, and for an
Inbox window: the Default Node opens with that window focused.

### F-SCAN-3

Open the list, then close the target window from another view and press
Enter: Slash shows **Window closed** and stays open with the query intact.
Move the target to another Node instead: Enter opens the new Node.

## Decisions

- Module name `windows`, matching the existing noun modules.
