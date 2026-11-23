# Launch and close a nested Shell

Supports [Launch Mothership Shell in COSMIC](../goals/launch-native-shell.md).
The caller already has a working host Wayland session.

| Step | Part | Action |
| --- | --- | --- |
| Isolate | Session command | Resolve the existing parent display. Create a private runtime directory for its own children. |
| Start | desktopd → WM → Shell | Start each part only when its dependency is ready. The WM offers Home and a child socket. The Shell subscribes and maps its fixed entry. |
| Operate | User → Shell | Open the existing [Command Center](use-command-center.md) from its fixed entry. Dismiss it without trapping keyboard or pointer input behind an invisible overlay. |
| Stop | Session command | On exit or failure, stop only the processes it started and remove only this session's runtime directory. Preserve the host environment and other sessions. |

### F-LAUNCH-1

From COSMIC, one command reaches Home and displays only the fixed Command
Center entry. It does not require a socket name or another terminal.

### F-LAUNCH-2

Open the Command Center, then run its existing create, attach/move, Fly
and Home flow with two real disposable windows. Dismiss it and confirm
the invisible area does not take the apps' input.

### F-LAUNCH-3

Stop the session and relaunch immediately. A missing or failing component
produces a part-specific error and removes only that run's processes and
sockets, while the COSMIC parent remains responsive.
