# Mothership WM project instructions

Read the root README, the root architecture, and the affected part's README.
Keep design in root docs and part-specific setup next to the code. Make the
next useful change, not a general framework. Go Task is the root entry point;
local and future CI checks should call the same tasks.

## Knowledge

Product knowledge lives in [docs/software-knowledges](docs/software-knowledges/README.md).
Use [System](docs/software-knowledges/system/README.md) for the product boundary,
[Goals](docs/software-knowledges/goals/README.md) for outcomes,
[Flows](docs/software-knowledges/flows/README.md) for user and system behavior,
and [Artifacts](docs/software-knowledges/artifacts/README.md) for the models and
rules those flows use. Keep technical decisions in
[docs/architecture](docs/architecture/README.md). Link to the owning document
instead of copying it.

For a new feature or a knowledge or contract change, use the
[software-knowledge skill](.agents/skills/software-knowledge/SKILL.md).
Read the affected sources and choose the owner. If the area has no Goal or Flow,
use the templates to draft the missing knowledge before proposing implementation.
Add only affected rules and contracts. Review unapproved design with the owner.
A behavior-preserving fix within approved knowledge needs no new documents.

Before implementing or reviewing Center Map or shell UI, open the
[mothership-shell-design skill](.agents/skills/mothership-shell-design/SKILL.md),
then the linked UI spec, source assets and reviewer checklist. This
applies to delegated agents too. In a spawn brief, include the affected
feature, exact rule/reference paths and required native evidence; do not
assume the new agent has the chat or its screenshots.

Use the project tooling and its local instructions. Do not add global tools,
extra process gates, duplicate runbooks, or stale copies of task handoffs.

## Implementation

Keep WM window/input/render state distinct from flow policy and GTK UI state.
Do not send frame data or pointer events over service IPC. Before a handoff,
record the decision, changed paths, tests run, and what remains unproved.
A successful scaffold check is not native runtime acceptance.

Do not start or replace the user's desktop session, install native dependencies,
push, or configure hosted CI unless that action is requested and authorized.
