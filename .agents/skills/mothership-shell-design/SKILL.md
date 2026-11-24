---
name: mothership-shell-design
description: Apply and review the Mothership shell design system for the bottom rail, controls, telemetry, Overview Map and Node cards. Use for shell mockups, Penpot edits, reference selection, and visual acceptance before native shell changes.
---

# Mothership shell design

1. Read the [canonical shell design system](../../../docs/software-knowledges/system/shell-design.md).
   Use its semantic roles and component contracts. Separate measured board
   values from rules that apply across viewports.
2. Inspect the current full Overview and the affected detail. Work in context:
   change only the authorized artifact and preserve surrounding layers.
3. Apply the shared chamfer recipe to shell corners in scope. At matched
   zoom, confirm a straight 45° cut (equal absolute x and y runs), small
   cuts sized by role, and one thin parallel inset seam or highlight with
   constant perpendicular spacing. No stacked strokes; round only when the
   owner expressly requests it.
4. Give every recess a named occupant. Size and align both together so the
   occupant visibly uses the pocket. At matched zoom, check positive,
   even painted-edge gaps; for a fitted pocket, measure equal perpendicular
   gaps between matching straight and diagonal edges, not between boxes.
   Stop at each seam mouth; no wrapping or overhanging spur. Trim or bevel
   an exposed end inward rather than inventing a mating edge.
5. Show the full render and matching detail crops. Report deviations,
   mock-only states, and any reference or measurement you could not verify.
   Get the owner's design approval before native work; visual review alone
   does not authorize edits.
