# Window manager

Planned Go executable. Owns windows, input, focus, geometry, the 2D camera
and rendering. Use a narrow cgo boundary to wlroots. No code in this part yet.

First runtime target: a nested Wayland session under Niri with two real clients.
The desktop shell and flow service are not required to render or accept input.
