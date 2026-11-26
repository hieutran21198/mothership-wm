# Organize open windows from Command Center

Tracking: [GitHub issue #6](https://github.com/hieutran21198/mothership-wm/issues/6).

A person can create Nodes, move individual live windows and switch views
from a native Command Center without using a terminal.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-COMMAND-1 | Create a named empty Node without selecting it. Attach an exact window from Home or another Node. | [F-COMMAND-1](../flows/use-command-center.md#f-command-1) |
| G-COMMAND-2 | Moving the focused window out of the selected Node keeps that Node open and sends the next key to a remaining visible window, or clears focus if none remains. | [F-COMMAND-2](../flows/use-command-center.md#f-command-2) |
| G-COMMAND-3 | Fly and Home explicitly switch views and leave existing clients and their contents intact. | [F-COMMAND-3](../flows/use-command-center.md#f-command-3) |
| G-COMMAND-4 | Remove an unused user-created Node, or move its windows to Default Node before removing it. Do not disrupt an attached agent manager. | [F-COMMAND-4](../flows/use-command-center.md#f-command-4), [F-COMMAND-5](../flows/use-command-center.md#f-command-5), [F-COMMAND-6](../flows/use-command-center.md#f-command-6) |

A Node is presented as a named [Domain](../artifacts/domain.md). No map,
preview, animation, new layout, launcher, agent or persistence.
