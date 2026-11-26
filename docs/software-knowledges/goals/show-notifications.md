# Identify and revisit notifications

The desktop user can tell where a notification came from and read it
later without letting pop-ups interrupt every view.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| Source | A toast names the Node and app. Ambiguous senders do not borrow the selected Node or Home. | [Ingress](../flows/show-notifications.md#ingress) |
| History | The Home bell exposes one history grouped by Node, including Home. Dismissing a toast or turning toasts off does not delete history. | [Reading](../flows/show-notifications.md#reading) |
| Map feedback | New Node activity has a short, distinct signal and an unread badge without moving the camera. | [Map feedback](../flows/show-notifications.md#map-feedback) |
