# Route input between two nested windows

**Implemented for the single-seat nested slice.** Supports [Focus one of two nested windows](../goals/focus-nested-windows.md).
A running nested COSMIC session and two disposable xdg-shell windows are the
test boundary. No interaction with an unrelated window or service is required.
WM owns the local state and follows the [Window rules](../artifacts/window.md).

### F-WM-FOCUS-1

**Given** a [ready nested Desktop Session](../artifacts/desktop-session.md),
**when** two windows with the same app ID map as A and B, **then** they are
visible as distinct input targets and the test can observe two different WM
IDs. This does not prescribe a general layout. Covers G-WM-FOCUS-1 and WIN-1.

### F-WM-FOCUS-2

**Given** A and B are live, **when** the developer clicks A and sends a
harmless key, then clicks B and sends a different key, **then** each click is
delivered only to its hit target and each key only to the focused window.
The non-target receives no corresponding button or key event. Covers
G-WM-FOCUS-2 and WIN-2.

### F-WM-FOCUS-3

**Given** B has keyboard focus and A is still mapped, **when** the
disposable B client closes its window, **then** B is no longer an input
target and the next key goes only to A. If no eligible window remains, the
WM clears keyboard focus. This does not define ordering among multiple
fallback targets. Covers G-WM-FOCUS-3 and WIN-3.

## Acceptance record

Execution evidence and the completion checklist are kept in
[GitHub issue #1](https://github.com/hieutran21198/mothership-wm/issues/1).
The scenarios above define scope; they do not report the run.
