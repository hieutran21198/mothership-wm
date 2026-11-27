# See where the day went

**Status:** design accepted 2026-11-27; not implemented.

The desktop user needs to see how long they spent in each Node, today and on
recent days, because Nodes map to projects and the shell already knows when
each one is entered.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-MISSION-1 Record | Time in a named Node or Default Node is recorded from acknowledged entry until another view, lock or session end. Home, Overview and Map are not counted. | [F-MISSION-1](../flows/review-mission-log.md#f-mission-1) |
| G-MISSION-2 Review | The Mission Log shows one day's timeline per Node with totals, including the live segment, and steps through earlier days. | [F-MISSION-2](../flows/review-mission-log.md#f-mission-2) |
| G-MISSION-3 Local | Only Node names and times are stored, locally, for 30 days. History survives Node deletion and can be cleared. | [F-MISSION-3](../flows/review-mission-log.md#f-mission-3) |

No window titles, app names, screenshots, export or remote sync.
