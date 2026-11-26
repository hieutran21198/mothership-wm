# Use Mothership Dev

Supports [Use a native Mothership developer session](../goals/use-native-dev-session.md).
The existing nested mode remains available and does not claim the local
seat. The native slice publishes one DRM output. It does not implement
screen locking, XWayland, or multi-output management.

## Test on a local TTY before registering the login entry

1. Run `devenv shell --offline -- task mothership:candidate`. Keep the
   printed absolute candidate path. A build does not start a session.
   Run `task mothership:smoke-native CANDIDATE=/absolute/candidate`
   to prove startup and teardown without touching a real display.
2. Save work. Use an unused local TTY and log in as a normal user. From
   the repository, run the selected build in the panel environment:

   ```sh
   devenv shell --offline --profile panel-native -- /absolute/candidate/mothership --native
   ```

   If needed, set `MOTHERSHIP_OUTPUT=DP-1` to choose a connector and
   `MOTHERSHIP_OUTPUT_SCALE=1.5` to set the output scale before the
   command. With no connector override, the first available DRM output
   is used. Do not run the command with sudo or in a COSMIC terminal.
3. Check physical pointer and keyboard, open a terminal and Brave in the
   Node that requested them, test Alt+Tab and Win+1–9, then use
   Ctrl+Alt+Esc to exit. Ctrl+Alt+F1–F12 switches to a VT when the host
   permits it. Confirm the prior desktop remains usable. Do not leave the
   Dev session unattended as a locked desktop: its Lock action is disabled.

## Register and use the Dev login

After the TTY acceptance, import `nix/mothership-dev.nix` in the host
NixOS configuration and set `programs.mothershipDev.enable = true;`.
Add the session entry in one NixOS rebuild; do not set it as the default.
The installed launcher runs as the logged-in user. It refuses a missing
candidate and never starts the Wayland server as root.

Run `task mothership:select CANDIDATE=/absolute/candidate` as the same
user to select an already tested build. Select **Mothership Dev** at
login. The login entry never builds from the source tree. It starts the selected
candidate and reports its path in the `MOTHERSHIP_BUILD` startup line.
Updating the candidate selection does not restart a session.
Save work and log out and in to apply a changed WM or desktopd. Use
`task mothership:rollback` to choose the previous build before logging in.
If the Dev build cannot start, choose COSMIC or use TTY to roll back.
Keep both candidate directories. Panel-only replacement without logout
is a separate change; V1 has no live replacement command.

## Change one native display

1. From blank Home, press comma and open **Display**. The shell shows the
   monitor and connector, and queries the WM for the active mode and
   available resolution/refresh combinations. The nested session does
   not show this entry or change its parent display.
2. Select **Resolution**, **Refresh rate**, and **Scale**. Changing the
   resolution filters the refresh rates. **Cancel** or Escape discards
   the draft. **Apply** requests an immediate mode change without a timer.
3. WM validates and tests the mode and only then commits it. On success,
   relayout the same session and save `display` in the shared user config
   for this connector. If validation or modesetting fails, keep the
   current output. If the mode is active but saving fails, report it
   separately. On the next login, an unavailable saved mode falls back
   to the preferred mode. This does not enable a second output.

For acceptance, apply an advertised alternate mode, a supported scale,
and the original mode again. Check the new logical size, pointer bounds,
live applications, the dock and Map. Reopen Display and verify the
current mode. Check Cancel and restart persistence independently. Do
not treat a headless result as a DRM test.

## Verification boundary

A headless smoke test can prove startup, shell mapping, Node selection
and clean teardown without touching an existing desktop. It cannot
prove DRM/Modesetting, physical input, VT switching, or greeter launch.
G-DEV-1 and login/logout acceptance require the physical TTY and greeter
checks above. Do not make this the default session from headless evidence.
