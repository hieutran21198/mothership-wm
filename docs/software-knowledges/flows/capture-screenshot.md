# Select and export a screenshot

Supports [Capture a screenshot](../goals/capture-screenshots.md).
The first release covers the single output Mothership manages. The COSMIC
reference is the `Print` entry point and the three capture modes.
[System76 shortcuts](https://support.system76.com/support/pop-cosmic-keyboard-shortcuts/#other-system-shortcuts) ·
[COSMIC capture flow](https://github.com/pop-os/xdg-desktop-portal-cosmic/blob/master/src/screenshot.rs)

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Open | WM and Panel | **Win + Shift + S** and **Print Screen** open the tool from an app or shell view. If the parent compositor handles either shortcut, use **Screenshot** in Command Center or enter `screenshot` in Slash. The Panel dismisses the invoking menu before the WM takes the snapshot. |
| Show | WM and Panel | The WM snapshots the composed output before the snipping controls appear, with the pointer left out of the image. The Panel opens a temporary overlay above the current view. The WM routes input to it while it is active. |
| Select | Panel and WM | Start in **Area**. Drag, move, or resize a rectangle and show its final width and height in image pixels. In **Window**, outline the topmost visible window under the pointer and select it with a click. In **Display**, select the whole output. |
| Export | WM and Panel | Area and Display use the frozen frame. For Window, the WM renders the selected window and its Mothership frame at confirmation. **Copy** offers the PNG to the session clipboard. **Save PNG** commits a uniquely named file under Screenshots in the XDG Pictures folder. |
| Close | Panel and WM | Show the completed action and restore focus after export. `Esc` discards an uncommitted capture. If the previous target has closed, use the WM's normal focus fallback. |

## Overlay design

The [interactive design preview](../system/screenshot-preview.html) uses a
sample desktop and produces PNGs from that scene.

Dim the unselected area over the frozen image. Use a crisp outline and
visible resize handles. Place one compact toolbar above the bottom rail
with **Area**, **Window**, **Display**, **Copy**, **Save PNG**, and a cancel control. Use the
shell's cool tint, restrained cyan accent, and small 45° corner cuts.
Distinguish the selected mode from keyboard focus. Keep the toolbar inside
the viewport at narrow widths. Enable export when there is a valid selection.
`Tab` reaches the controls, `Enter`
activates the focused control, and `Esc` cancels.
[Shell design system](../system/shell-design.md) ·
[Analog button reference](../system/home-panel-references/Analog-Button-Open-Corners.png)

## Ownership and recovery

The WM owns composition, output scale, target validation, and input routing.
The Panel owns controls, destination selection, and feedback. Use the
launcher-authorized Panel connection for the capture handoff and keep image
buffers on the Wayland path. Delivery commits locally when the file is
published or the session clipboard accepts the offer. Keep the clipboard
source alive after the overlay closes so the next app can paste it.
[Architecture boundaries](../../architecture/README.md)

On export failure, keep the selection visible and show the error and available
actions. Clear a window selection if the target closes. Cancel the capture
when the output size or scale changes, the seat loses focus, or the Panel
disconnects. Release the frame and restore normal input routing.

## Selection

| Scenario | Given | When | Then | Covers |
| --- | --- | --- | --- | --- |
| Global shortcut | An app holds focus | The user presses **Win + Shift + S** or **Print Screen** | The overlay opens once. The app receives no screenshot trigger key. | Select |
| Pixel bounds | A real Wayland client shows known edges at 100% and 150% scale | The user drags in either direction, resizes against the output edge, and exports | The decoded PNG dimensions and pixels agree with the selection. The image contains only the selected scene pixels. | Select |
| Frozen frame | A client updates its contents continuously | The user opens the overlay, waits, and saves an Area or Display capture | The PNG matches the frame shown under the overlay. | Select |
| One window | Two real windows overlap | The user selects the partly covered window by its exposed area | The PNG contains just that window and its frame. Selecting Display produces the full composed output. | Select |

## Export

| Scenario | Given | When | Then | Covers |
| --- | --- | --- | --- | --- |
| File or clipboard | A valid selection in the nested session | The user saves twice, then copies and pastes in another nested Wayland app after the overlay closes | Both PNG files have distinct names. The paste has the captured pixels and the clipboard target is the child seat. | Deliver |

## Recovery

| Scenario | Given | When | Then | Covers |
| --- | --- | --- | --- | --- |
| Cancel | A selection and an app that previously held focus | The user presses `Esc` | The pending image is discarded, and fresh typing goes to the app. | Return |
| Bad destination | The save destination is unwritable | The user chooses **Save PNG** | The Panel reports the error, preserves the selection, and offers Copy or Cancel. | Deliver, Return |
| Lost capture | The overlay owns input | The output changes, the seat loses focus, or the Panel exits | The WM releases the snapshot and clears capture input state. Fresh input follows the normal focus rules. | Return |

Review full overlay views and detail crops over light and dark content, at
100% and 150% scale. Run the scenarios first with real apps in a disposable
nested session, then in an authorized [native developer session](use-native-dev-session.md).
