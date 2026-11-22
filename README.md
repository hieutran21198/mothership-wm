# Mothership WM

A Go window manager and desktop shell for Linux/Wayland. One 2D canvas, with
windows organized into intentional flows rather than separate workspaces.

**Status: project scaffold only.** No native binaries, compositor, CI workflow,
or desktop integration yet.

## Development

From the repository root:

```sh
devenv shell
task
task versions
```

`go.mod` is the Go version authority. The initial Nix input lock is carried
over from the Platform toolchain, using the same input definitions. The
Mothership shell has not yet been evaluated with those inputs. There is no
Node.js, AWS, Terraform, deployment profile, or automatic hook setup.

## Project layout

- [apps/wm](apps/wm/README.md): compositor and window policy.
- [apps/panel](apps/panel/README.md): GTK4 shell surface and command palette.
- [services/flow](services/flow/README.md): flow membership and command routing.
- [docs/architecture](docs/architecture/README.md): system boundaries and first milestones.

Use one Go module until an independently versioned module is actually needed.
Add build/test tasks when the first real package lands and have CI call those
tasks, not duplicate them.
