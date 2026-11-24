# System

Write here when product scope, actors, or a responsibility shared by parts
changes. Keep technical choices and their rationale in
[architecture](../../architecture/README.md). Link to Goals for outcomes,
Flows for behavior, and Artifacts for domain rules.

- [Mothership WM system overview](mothership-wm.md): actors, current slice, and planned parts.
- [Center Map UI contract](center-map-ui.md): layers, Node Detail, and minimap design.

- [Mothership shell design system](shell-design.md): shared visual rules,
  component contracts, and review checks; not a runtime contract.

## Template

```markdown
# <system or changed boundary>
Purpose and scope: <what it covers>
Actors: <actor — purpose — boundary>

| Part | Owns | Does not own |
| --- | --- | --- |
| <part> | <responsibility> | <out-of-scope responsibility> |

Shared requirements or external interfaces: <only those affected>
```

Omit optional sections if they do not apply. Keep a shared glossary or diagram
only when it clarifies the boundary.
