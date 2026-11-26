# Open Home and use Inbox

Supports [Use Home to find unassigned windows](../goals/use-home-inbox.md).
Home is the shell, not a named Domain. The internal Default Node renders the
[Inbox membership](../artifacts/domain.md#home-and-inbox) when foregrounded.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Start | WM → desktopd | Start managed sessions at blank Home. Standalone WM remains unchanged. |
| Map | WM → desktopd | A live, mapped unassigned window is in Inbox. Keep it off other projections. An explicit Home launcher may foreground the Default Node. |
| Enter | Panel → desktopd → WM | **W** from focused Home with no other overlay opens the enabled Default Node. Apply its last acknowledged [Node layout](view-node-layouts.md#f-view-2), or Flow on first entry. Restore last valid focus. |
| Assign | Local client → desktopd → WM | Assign the exact live Window to an existing named Domain. Remove it from Inbox, without changing the active workspace. |
| Leave | WM → Panel → desktopd → WM | Bare **Meta** release from a focused app or Default Node returns to blank Home. A Meta chord, interrupted tap, or another mode is not this action. Keep windows alive and preserve their layout. |

### F-HOME-1

Start a managed session. Open two unassigned native windows without using the
Home launcher; they remain live and listed but do not appear on Home. Press
W and check both framed windows use Flow. Click a preview or use Alt+Tab
to focus the intended window without moving it out of Inbox. In Node
Panel Settings, select Stage and then return to Flow. The same focus,
maximize/restore and close rules apply as in a named Node. No Default
Node entry is added to Center Map or mini-map.
A host compositor may reserve physical Meta or Alt+Tab before Mothership sees it.

### F-HOME-2

Assign an Inbox window to a named Domain. Its exact live window leaves the
Default Node, not other windows or the whole Node. New unassigned windows do not
appear or take focus in a named Domain. A named `Home` or `Main` is distinct
and uses the same rules as other named Domains.

### F-HOME-3

Tap bare Meta; the shell shows Home without redirecting input back to an app.
Re-enter with W and check that the remembered layout, window order and focus are restored. A named Node keeps its own mode.
Close the last-focused app while hidden and re-enter: use another eligible
window or leave focus clear. Hold Meta with another key, or change host focus
mid-tap: do not return Home. W in an app is an ordinary key. Set
`"home": {"default_node": false}` in the sparse user config, then R on blank
Home; W no longer enters, and live Inbox windows stay available for explicit
assignment. Re-enable and repeat. Only a WM acknowledgement changes selection.
