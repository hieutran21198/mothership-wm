# Use a native Mothership developer session

The owner can choose **Mothership Dev** at login and test new builds
without rebuilding NixOS on every code change. Keep COSMIC installed and
keep the existing default session. This is not the stable distribution
package or a full desktop replacement.

| Criterion | Observable success |
| --- | --- |
| G-DEV-1 | One real monitor and a local keyboard and pointer work without a parent compositor. A terminal and one real native Wayland app open in a Node. Alt+Tab and Win+1–9 reach Mothership. |
| G-DEV-2 | The owner can exit to login or TTY. The next boot can still select COSMIC. A failed Mothership boot does not edit the system default. |
| G-DEV-3 | Building and testing does not replace a running build. Choosing a candidate changes what the next Mothership login starts. Choosing rollback selects the previous candidate. |

[Native developer session flow](../flows/use-native-dev-session.md) owns
the setup, acceptance, and update procedure.
