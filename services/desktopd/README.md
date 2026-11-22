# desktopd

A local Go service that owns live Window-to-Domain assignments and resolves
[FocusDomain](../../docs/software-knowledges/flows/focus-domain.md). The WM
owns visibility and the human seat’s focus. Domain state is in memory only.

- `cmd/desktopd`: process composition.
- `internal/app`: Domain commands and WM session lifecycle.
- `internal/domain`: assignment rules and state.
- `internal/adapter/unixapi`: private Unix socket and CLI transport.

## Run

From the repository root, build the two binaries:

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
bin/mothership-desktopd create A
bin/mothership-desktopd create B
bin/mothership-desktopd list
bin/mothership-desktopd assign SESSION WINDOW_ID A
bin/mothership-desktopd assign SESSION OTHER_WINDOW_ID B
bin/mothership-desktopd focus A
```

Replace the uppercase values with the IDs shown in `list`. Use Ctrl+C in
each process’s terminal to stop it. No daemon is installed to run at login.
