# desktopd

A local Go service that owns live Window-to-Domain assignments and resolves
[FocusDomain](../../docs/software-knowledges/flows/focus-domain.md). The WM
owns visibility and the human seat’s focus. Live membership, layout and focus remain session-only. Node names,
descriptions, timestamps and assigned Map shortcut slots are stored as private records in
`$XDG_STATE_HOME/mothership-wm/nodes` (default `~/.local/state/mothership-wm/nodes`).
Set a disposable absolute `XDG_STATE_HOME` when testing.

- `cmd/desktopd`: process composition.
- `internal/app`: Domain commands and WM session lifecycle.
- `internal/domain`: assignment rules and state.
- `internal/adapter/unixapi`: private Unix socket and CLI transport.
- `internal/adapter/tray`: event-driven StatusNotifier host and DBusMenu
  routing on the [private session app bus](../../docs/software-knowledges/flows/use-system-tray.md).

## Run

From the repository root, build the two binaries. The `wm-native` profile
includes GIO for the tray adapter (`-tags tray`); plain Go tests do not
require it. The supervised `mothership` launch starts the app bus; running
these two binaries alone does not enable the tray.

```sh
devenv shell --profile wm-native -- task wm:build desktopd:build
```

In one terminal, run `bin/mothership-desktopd serve`. In another terminal in
the same COSMIC session, run `bin/mothership-wm --desktopd`. The WM prints
the dedicated child socket. Start disposable native clients using that socket
as described in the [WM README](../../apps/wm/README.md#manual-acceptance).
Do not set the nested socket for the whole desktop.

From the repo root in another terminal, create Domains, read the current
session and Window IDs, assign explicitly, and select a view:

```sh
bin/mothership-desktopd create A "Backend Platform"
bin/mothership-desktopd create B
bin/mothership-desktopd list
bin/mothership-desktopd assign SESSION WINDOW_ID A
bin/mothership-desktopd assign SESSION OTHER_WINDOW_ID B
bin/mothership-desktopd focus A
bin/mothership-desktopd home
```

Replace the uppercase values with the IDs shown in `list`. Use Ctrl+C in
each process’s terminal to stop it. No daemon is installed to run at login.

Other local clients use the [desktopd JSON socket contract](../../docs/architecture/desktopd-client-ipc.md).
