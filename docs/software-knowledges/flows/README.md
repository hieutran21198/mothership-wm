# Flows

A Flow owns sequence, handoffs and failure behavior. Add or update it when those
promises change. Name who executes each step and where work commits locally;
a cross-part call does not imply a shared transaction. Link the receiving
Artifact for its operation, payload and errors rather than duplicating them.

## Template

```markdown
# <journey>
Trigger and preconditions: <what starts this flow and when>
Participants and outcome: <actors/parts; observable result>
Local transaction or state change: <owner and boundary, if applicable>
Failure and recovery: <rejection, partial result or retry, if applicable>

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| <step> | <responsible part> | <operation; result; contract link if needed> |

| Scenario | Given | When | Then | Covers |
| --- | --- | --- | --- | --- |
| <scenario label> | <starting facts> | <trigger> | <observable result> | <criteria/rules> |
```

The scenario is expected behavior, not a claim an acceptance check ran.
