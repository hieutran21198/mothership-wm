# Open a terminal or file manager from Command Center

Tracking: [GitHub issue #8](https://github.com/hieutran21198/mothership-wm/issues/8).

| Criterion | Observable outcome | Flow |
| --- | --- | --- |
| G-APPS-1 | Terminal and File Manager each open a fresh native window inside Mothership, never by reusing a host desktop instance. | [F-APPS-1](../flows/open-native-apps.md#f-apps-1) |
| G-APPS-2 | New windows start unassigned in Home / Inbox. A selected Node and its keyboard target do not change; Attach / Move remains explicit. | [F-APPS-2](../flows/open-native-apps.md#f-apps-2) |
| G-APPS-3 | A missing or failing app is reported without changing the view, and session exit stops only app instances the Shell started. | [F-APPS-3](../flows/open-native-apps.md#f-apps-3) |

No general application catalog, arbitrary launch commands, sandbox, or changes to app themes.
