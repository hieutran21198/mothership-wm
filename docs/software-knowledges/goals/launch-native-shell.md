# Launch Mothership Shell in COSMIC

Tracking: [GitHub issue #7](https://github.com/hieutran21198/mothership-wm/issues/7).

A developer can open a nested Mothership Shell from one local command
without changing the current desktop session.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-LAUNCH-1 | One command opens Home and an entry for the existing native Command Center. | [F-LAUNCH-1](../flows/launch-native-shell.md#f-launch-1) |
| G-LAUNCH-2 | Existing create, assign/move, Fly, and Home behavior remains available with real windows. | [F-LAUNCH-2](../flows/launch-native-shell.md#f-launch-2) |
| G-LAUNCH-3 | Startup errors identify the failed part; a normal exit or failed start leaves no children or sockets owned by this session. | [F-LAUNCH-3](../flows/launch-native-shell.md#f-launch-3) |

The shell, WM and desktopd are separate. No map, preview, animation,
layout modes, app launcher, agent, desktop registration or app theme changes.
