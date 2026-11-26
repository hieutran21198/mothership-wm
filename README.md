# Mothership WM

A Go window manager and desktop shell for Linux/Wayland. One 2D canvas, with
windows organized into intentional flows rather than separate workspaces.

**Status:** GTK Shell, a Wayland WM, and a local Domain service. The nested
session remains the known-good path. A one-output native developer session
is available for TTY acceptance; it does not replace the default desktop.

## Run inside COSMIC

From the repository root in the existing session:

```sh
devenv shell --offline -- task mothership:run
```

This builds and starts the three local parts, opens Home and shows the
**Command Center** entry. Close the outer Mothership window or press Ctrl+C in
the launch terminal to end this private nested session.
The [local Shell guide](apps/panel/COMMAND_CENTER.md) covers the controls
and disposable application testing. User preferences live in
[`~/config/.mothership/config.json`](apps/panel/CENTER_MAP.md#user-configuration).

## Development

From the repository root:

```sh
devenv shell
task
task versions
```

`go.mod` is the Go version authority. The initial Nix input lock is carried
over from the Platform toolchain, using the same input definitions. There is no
Node.js, AWS, Terraform, deployment profile, or automatic hook setup.

### Build a candidate beside the current session

`devenv shell --offline -- task mothership:candidate` runs the local
checks and builds all session binaries under a new `bin/candidates/`
directory. It leaves the running session and its binaries alone. Test
the previous and candidate builds independently. To start a nested test
session from the candidate, use the exact printed directory:

```sh
devenv shell --offline --profile panel-native -- /path/to/candidate/mothership
```

This task does not apply an update. Keep the previous known-good build
available until the candidate passes a session test. See [Mothership Dev
login](docs/software-knowledges/flows/use-native-dev-session.md) for TTY
acceptance, candidate selection, and rollback. Live panel replacement is
not supported yet.

## Project layout

- [apps/wm](apps/wm/README.md): compositor and window policy.
- [apps/panel](apps/panel/README.md): GTK4 shell surface and command palette.
- [services/desktopd](services/desktopd/README.md): live Domain assignments and view selection.
- [services/flow](services/flow/README.md): flow membership and command routing.
- [docs/architecture](docs/architecture/README.md): system boundaries and first milestones.

Use one Go module until an independently versioned module is actually needed.
Add build/test tasks when the first real package lands and have CI call those
tasks, not duplicate them.
