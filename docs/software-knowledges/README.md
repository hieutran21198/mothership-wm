# Software knowledge

Start here for a new feature. These READMEs describe how to write knowledge;
they are not product requirements. Read the [architecture](../architecture/README.md)
and the relevant part: [WM](../../apps/wm/README.md),
[Panel](../../apps/panel/README.md), or [Flow service](../../services/flow/README.md).

| Layer | Owns |
| --- | --- |
| [System](system/README.md) | Purpose, actors, product scope, part responsibilities and shared requirements |
| [Goals](goals/README.md) | Actor, need, outcome and success criteria |
| [Flows](flows/README.md) | Trigger, steps, coordination, local transactions and recovery |
| [Artifacts](artifacts/README.md) | Model, lifecycle, rules and the receiving part's public contract |

## From request to change

1. Read the request, existing facts, and affected parts. Identify the actor's
   outcome and which part owns the decision. Trace the affected path in code
   if it exists; a design note does not prove running behavior.
2. Reuse the existing Goal and Flow. If the area has neither, draft a minimal
   Goal and Flow first; if only one is missing, draft that one. Criteria point
   to the Flow scenarios that demonstrate them. A behavior-preserving fix or
   a change covered by an approved design does not need new knowledge pages.
3. For cross-part behavior, give each Flow step an executor and distinguish a
   local change from a handoff. Add Artifact rules or a receiving contract only
   where they change, and update System only where its scope or responsibilities
   change. Link the owning document rather than duplicating it.
4. Keep proposals separate from settled behavior. Record only material open
   choices. Share the draft and observable acceptance checks for owner review
   before implementing changed or unapproved design. Otherwise implement the
   smallest approved change without an extra review step.
5. Run the checks that prove the relevant criteria and scenarios. Report what
   passed and what remains unverified. Correct approved knowledge in its
   owning document rather than creating a second source.

Use the request or an existing issue for decisions, progress and check results.
Do not create extra trackers, approval phases or delivery documents.

Adapted from Cirius Platform's
[Knowledge](https://github.com/hieutran21198/cirius-go-platform/blob/main/docs/governance/artifact-driven/knowledge.md)
and [Workflow](https://github.com/hieutran21198/cirius-go-platform/blob/main/docs/governance/artifact-driven/workflow.md).
The templates are local to Mothership and do not require Platform tooling.
