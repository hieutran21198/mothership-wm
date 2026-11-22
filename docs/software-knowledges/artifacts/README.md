# Artifacts

An Artifact is a knowledge ownership boundary, not automatically an app,
service or table. Document a model, lifecycle or rule here when its own part
can enforce it. A Flow may depend on those rules without owning them. Use
[architecture](../../architecture/README.md) for implementation boundaries.

- [Desktop Session](desktop-session.md): nested WM lifecycle, readiness and teardown.

- [Window](window.md): identity, focus and input rules for nested windows.

- [Domain](domain.md): design accepted ownership of live Window membership.

## Template

```markdown
# <artifact>
Serving part: <owner>
Owns: <model, lifecycle and rules>
Does not own: <adjacent responsibilities>

| Rule | Invariant or allowed state change |
| --- | --- |
| <rule label> | <what always holds or may change> |

Receiving contract, if changed: <operation, caller, inputs, outputs, errors>
Verification: <relevant Flow scenarios or checks>
```

Use the actual interface format if a separate contract is needed. Local IPC
does not require an HTTP or OpenAPI specification.
