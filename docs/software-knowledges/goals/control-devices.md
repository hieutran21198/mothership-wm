# Control sound and Bluetooth devices from the shell

**Status:** design approved 2026-11-27; implemented in candidate `build-5P0nY5Vw`, awaiting native acceptance.

The desktop user needs to change the speaker or microphone, set their
levels and connect Bluetooth devices without leaving Mothership for the
host's settings app. Today Home shows only a read-only output level, and
[Node Volume](../system/home-panel.md#node-volume) changes app streams,
not devices.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-DEV-1 Output | From Home, choose the output device, then set its volume and mute. Playing audio follows the choice. | [F-DEV-1](../flows/control-devices.md#f-dev-1) |
| G-DEV-2 Input | Choose the microphone and set its level and mute. A muted microphone is visible on the dock. | [F-DEV-1](../flows/control-devices.md#f-dev-1) |
| G-DEV-3 Bluetooth | Turn Bluetooth on or off, connect or disconnect a paired device, and pair a new one after confirming its code. A connected headset can then be chosen as the output. | [F-DEV-2](../flows/control-devices.md#f-dev-2) |
| G-DEV-4 Honest | A missing sound server, Bluetooth adapter or blocked radio is named in the board with what still works. Nothing claims success before the system reports it. | [F-DEV-3](../flows/control-devices.md#f-dev-3) |

Not a replacement for the host's settings: no Wi-Fi, display, power
profiles, per-port routing, codec choice or audio effects in this slice.
Device changes are machine-wide, so in a nested session they also change
the host desktop. Because they are wider than any Node, the board is
Mothership (Home) scope only; no Node panel opens it.
