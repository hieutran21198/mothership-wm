# Control devices from the Systems board

**Status:** design approved by the owner 2026-11-27 (with the projector unfold, signal beam and radar scan, and a Systems glyph instead of the speaker); implemented the same day in candidate `build-5P0nY5Vw`, awaiting native acceptance.
Supports [Control sound and Bluetooth devices from the shell](../goals/control-devices.md).
Design: [Systems preview](../system/systems-preview.html).

The Panel owns the Systems board and talks to the device services
directly, like [Node Volume](../system/home-panel.md#node-volume). desktopd
and the WM take no part: device state is machine state, not Node state,
and nothing here changes membership, focus or the view.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Open | Panel | The Home dock **Audio** button becomes **Systems**, drawn with the Systems glyph (three control tracks with knobs at different levels) instead of the speaker, and opens an analog glass board above it, styled like the [Notification Log](show-notifications.md#reading). The button keeps the cyan latched edge while the board is open. **D** on focused blank Home opens it too. Slash `systems` opens it on the last module, `systems sound` and `systems bluetooth` on that module, `systems scan` opens Bluetooth and starts a scan, and `systems mute` toggles the default output's mute without opening it. The board is Mothership scope: devices are machine-wide, wider than any Node, so nothing in a Node opens it. Node Slash has no `systems` module, Node Volume keeps only its per-app rows, and entering a Node closes an open board. Esc, the close button, a click outside or a view change closes it. It replaces the Notification Log, Mission Log and System Tray while open. |
| Unfold | Panel | With animations on, the board opens as a projection: a beam rises from the Systems button to the board's height (0–56 ms), the board unfolds sideways from it (56–175 ms) and one scan line settles it (175–280 ms), each with an ease-out. Progress follows elapsed time, so a slow frame skips ahead rather than stretching the motion, and the board's contents are drawn once and only clipped while it unfolds; row highlights start settled. Closing plays it backwards in 140 ms. Input works from the first frame and is released at the start of a close; Esc mid-unfold closes from the current state. Nothing redraws after it settles. With animations off it opens and closes at once. This replaces the log-style fade for this board only. |
| Layout | Panel | A module rail on the left (**Sound**, **Bluetooth**), each with a one-line status. The module content is on the right. **Tab** changes module; **W / S** move between rows; **A / D** adjust the focused level by 5%; **Enter** chooses or acts; **Space** toggles mute or power. Pointer works on every control. The board remembers the last module for the session. |
| Sound | Panel → PulseAudio | List sinks and sources with `pactl --format=json` (with `info` for the defaults). **Output** lists each sink by description with its kind (Built-in, Bluetooth, HDMI, USB). Choosing one runs `set-default-sink`; streams that follow the default move with it. Below it, one level (0–100%, no boost) and a mute button. **Input** does the same for sources, hiding monitor sources. Values update only from the server's next report. |
| Beam | Panel | A thin cyan beam runs from a small Mothership mark beside **Output** down a left gutter into the active output's row, with one 45° bend; a second beam runs from the active microphone into the mark beside **Input**. Choosing another device draws a dashed beam to it and shows **Switching…** while the old beam dims to 30%. When `pactl` reports the new default, the new beam turns solid and, with animations on, one bright head runs along it in 200 ms; the old beam goes. A failure retracts the dashed beam and shows the error on the row. A muted device dims its beam to 30%. No idle animation. |
| Dock | Panel | The Systems button adds a small struck speaker at its lower right while the default output is muted and a small struck microphone at its lower left while the default input is muted. To keep these current, one `pactl subscribe` runs for the session and samples only on sink, source, server or card events, coalesced for 120 ms; stream (sink-input) events are ignored, so playback costs nothing. It restarts with back-off when the sound server restarts. |
| Bluetooth | Panel → BlueZ | While open, the Panel watches `org.bluez` on the system bus with GDBus. A **Power** Off / On control sets `Adapter1.Powered`. **Paired** lists devices with an icon by class, name and status (**Connected · 80%** from `Battery1`, **Not connected**, **Connecting…**) and one action: Connect or Disconnect. **Scan** starts discovery for 30 s and shows **Nearby** as a radar: a 150° fan with the Mothership mark at its apex, Near and Far rings, and each unpaired device placed by its `Device1.RSSI` (stronger is closer; angle is stable per device, not a direction). A sweep line turns only while discovery runs. Named devices show name and kind; **W / S** step through them strongest first and **Enter** pairs the focused one. Unnamed devices are faint dots, counted and not selectable. A device without RSSI sits on the outer ring. A device's **Forget** sits behind its row's second press of Backspace. |
| Pair | Panel ↔ BlueZ | During a pairing the Panel registers a `DisplayYesNo` agent. A passkey or confirmation request shows the code inline: **Confirm 482 913 shows on JBL Flip 6**, with Reject and Confirm. A PIN entry request offers a field. Closing the board or 30 s without an answer rejects. Only the requested device is accepted. |
| Settle | Panel | A connected audio device appears in **Output** and **Input** after the server reports it. The board never changes the default itself; the sound server's own policy may. Errors show on the row that caused them (**Could not connect: device not reachable**) until the next action. |

Failure and recovery: without `pactl` or a PulseAudio-compatible server
the Sound module reads **Sound unavailable: no PulseAudio-compatible
server or pactl** and the dock button still opens the board. Without
`bluetoothd` or an adapter, Bluetooth reads **No Bluetooth adapter**.
An rfkill block reads **Bluetooth is blocked by the system (airplane
mode or a hardware switch)** with no Power control. A system bus policy
refusal is shown as returned. Closing the board stops discovery, drops the
agent and the watchers.

### F-DEV-1

Open Systems from the Home dock. Choose an HDMI output while a video plays:
the sound moves, and the row is marked only after `pactl` reports it.
Press D on blank Home: the board unfolds from the Systems button and
opens on Sound; with animations off it appears at once. Lower the level with A to
40%, mute with Space: the dock button shows the struck speaker mark. Choose another microphone and mute it: the dock shows
the microphone mark. Enter Node Ops with the board open: it closes. In
Ops, Slash shows no `systems` module and Node Volume has no Output row.

### F-DEV-2

With Bluetooth Off, turn it On: the paired list appears. Connect a paired
headset: **Connecting…** then **Connected · 80%**, and it appears in
Output. Choose it if the server did not already: the video plays through it.
Disconnect it: playback returns to the next sink chosen by the server.
During the switch the dashed beam and **Switching…** show until `pactl`
reports the default; then the beam is solid and nothing keeps animating.
Scan: the radar sweeps, and a speaker moved closer to the adapter moves
toward Near. When the scan ends the sweep stops and the dots stay. Focus
the speaker with W / S and press Enter on one that shows a code: Confirm pairs it;
Reject or closing the board leaves it unpaired.

### F-DEV-3

Stop `bluetoothd`: the module reads **No Bluetooth adapter** and Sound
still works. Block the radio with rfkill: the blocked message replaces the
Power control. Remove `pactl` from PATH: Sound shows the unavailable
message and Bluetooth still works. Close the board during discovery and
check with `bluetoothctl show` that discovery stopped.

## Contract

- Home dock: the Audio button becomes **Systems** with its own glyph and
  no longer posts the `wpctl` output notice; Home keyboard guide gains
  **D**; Slash gains `systems` with `sound`, `bluetooth`, `scan` and `mute`.
- Scope: Home only. A Node has no entry point; Node Volume is unchanged.
- Panel: a GDBus client of `org.bluez` on the system bus while the board
  is open or pairing. No desktopd or WM protocol change.
- Motion: the projector unfold, beam head and radar sweep follow the
  GTK animation setting and stop when settled; no idle redraw.

## Decisions

- Device connects the user did not start post no notice; the BlueZ
  watch runs only while the board is open or pairing.
- A newly connected headset becomes the output only by the sound
  server's own policy; the board never switches it.
- This slice is Sound and Bluetooth. Network (Wi-Fi), Display brightness
  and Power would be later modules on the same rail, each with its own
  Goal.
- Clicking a paired row, the adapter row or a level row only focuses
  it; its buttons act. A pending switch that `pactl` does not confirm
  within 3 s shows **Could not switch output** on that row.
