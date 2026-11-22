# Mothership WM project instructions

Read the root README, the root architecture, and the affected part's README.
Keep design in root docs and part-specific setup next to the code. Make the
next useful change, not a general framework. Go Task is the root entry point;
local and future CI checks should call the same tasks.

Keep WM window/input/render state distinct from flow policy and GTK UI state.
Do not send frame data or pointer events over service IPC. Before a handoff,
record the decision, changed paths, tests run, and what remains unproved.
A successful scaffold check is not native runtime acceptance.

Do not start or replace the user's desktop session, install native dependencies,
push, or configure hosted CI unless that action is requested and authorized.
