# Open and save files from an app

Design approved. See the Flow for implementation and runtime checks.

A user working in Mothership needs to open, save, or select files and folders
from an app while staying in the app's Node.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-CHOOSER-1 | A supported app opens a usable file or folder chooser and receives the selection or cancellation. | [F-CHOOSER-1](../flows/open-app-file-dialog.md#f-chooser-1) |
| G-CHOOSER-2 | A chooser with a Wayland parent stays with that app in Flow or Stage. Closing the chooser restores input to the parent. | [F-CHOOSER-2](../flows/open-app-file-dialog.md#f-chooser-2) |
