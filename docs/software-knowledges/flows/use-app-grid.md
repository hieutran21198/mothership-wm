# Browse and launch from App Grid

Supports [Find and launch installed apps from App Grid](../goals/use-app-grid.md).
The [App Grid contract](../system/app-grid.md) owns presentation, pins and
launch policy. The Panel holds pagination and focus for the current opening.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Open | User and Panel | Press **A** on blank Home, click the Home dock's App Grid button, or use **Meta+A** in a Node. The Panel focuses Search and opens Pinned page 1 if there are installed pins, otherwise All Apps page 1. |
| Browse | User and Panel | Use Previous and Next or move horizontally past a page's first or last tile. The order is all Pinned pages, then all All Apps pages. Moving backward focuses the previous page's last tile. Moving forward focuses the next page's first tile. Unavailable directions leave the page and focus in place. |
| Search | User and Panel | Editing the query filters the full catalog and starts on page 1. Clearing the query restores the opening page. Left and Right edit text while Search has focus. Down enters the displayed results. |
| Activate | User and Panel | Click or press Enter on a tile to request launch. Enter in Search requests the first app on the displayed page. |

## Scenarios

| Scenario | Given | When | Then |
| --- | --- | --- | --- |
| Browse pages | Home or a Node and a catalog of more than 20 apps | Open App Grid with zero pins, then with enough pins for multiple pages. Use the pager and both sets of direction keys. | The first page reflects installed pins. Every page has at most 20 entries. The counter and buttons reflect the current section and available directions. Both directions cross the Pinned and All Apps boundary and stop at the catalog ends. |
| Search and launch | An app on a later catalog page | From the opening page, type its name or ID in Search, edit with the arrow keys, press Down and activate the result. | The matching app appears on page 1 and launches under the existing launch policy. |
