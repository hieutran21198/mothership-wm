# Show ship status on the Center Map

**Status:** Cloak, its command packet and the Detail Silent running button implemented 2026-11-27 and owner accepted in a native Mothership Dev session (candidate `build-zvH5676i`) on 2026-11-27; the Detail Loadout and Today rows implemented with their features; Comms arcs not implemented.
Supports [Read each Node's state from the Center Map](../goals/show-ship-status.md).
Design: [Fleet Ops preview, scene 1](../system/fleet-ops-preview.html).

Status is paint on the existing world layer of the
[Center Map](../system/center-map-ui.md). It adds no hit targets and does
not change marker size, label position, route endpoints or the minimap.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Sample | Panel → PulseAudio | Only while the Map is visible, subscribe with `pactl subscribe` and resolve playing sink inputs to Nodes with the [Node Volume](../system/home-panel.md#node-volume) scope rules. Shared or unidentifiable streams do not mark any Node. Unsubscribe when the Map closes. |
| Comms | Panel | Draw three short arcs ahead of the ship's bow in the marker ink. With motion, they pulse outward on a 1.2 s loop that pauses with the route animation rules. Motion Off shows static arcs. A Node muted by Node Volume shows the arcs struck through and static. |
| Cloak | Panel | For a Node whose [Silent running](run-silent.md) preference is not Off, draw the hull outline dashed and the dark planes at about 50% opacity. The label, focus treatment and unread badge keep full contrast. |
| Command | Panel | The Node Detail **Silent running** button saves the change and Mothership sends the command packet at once; the button shows the saved state immediately and the ship changes when the packet arrives. A Silent running change made while the Map is closed waits for the next Map open. After the opening radar, Mothership sends one straight cyan command packet with a bright chevron head along that Node's route (about 550 ms). On arrival a cyan ring closes on the ship while the hull morphs between solid and cloaked (about 150 ms). Turning it off sends the same packet and morphs back. Until arrival the ship shows its previous state. Commands fly before notice pulses, one packet at a time. Toggling on and off before the Map opens sends nothing. With Motion Off, when Mothership or the ship is out of view, or if the Map closes mid-flight, the ship shows the saved state without a packet. Do not move the camera or recolor the route. |
| Combine | Panel | Badge, comms and cloak can co-exist. Selection and focus treatment stay on top. Hide comms arcs during the radar, Arcade and Connect relay, like Node Detail. |
| Detail | Panel | Node Detail adds **Loadout · N apps** from [the loadout](launch-node-loadout.md) and **Today · 1h 42m** from [the Mission Log](review-mission-log.md), only when present. Keep the card's “no dummy fields” rule. |

### F-SHIP-1

Play audio in Ops and open the Map: Ops shows animated arcs and no other
Node does. Mute Ops from Node Volume: arcs are struck through. Stop
playback: arcs disappear within one second. Set Motion Off: arcs are
static. Close the Map and confirm with `pactl` client listing that the
subscription has ended. Pan and zoom: arcs stay attached to the bow and
the camera does not move on status changes.

### F-SHIP-2

Enable Silent running on Research and open the Map over a light and a dark
wallpaper: the cloaked hull is distinct from idle but the label reads at
the same contrast. Select Research: the selected outline and route are
unchanged. Receive a notice in Research: the badge shows on the cloaked ship.

Turn Silent running on in Research and off in Ops, leave a notice unread in
Ops, then open the Map. After the radar, one cyan packet flies to Research
and it cloaks on arrival, then one to Ops and it decloaks, then the lavender
notice pulse returns from Ops. Toggle Review on and off before opening: no
packet. With the Map open, select Research and click the crescent in its
Detail card: the button lights at once and a packet flies to Research,
which decloaks on arrival. Click twice quickly: two packets fly and the
ship ends on the saved state. Repeat with Motion Off and with Research panned out of view: both
ships show the saved state at once. Close the Map mid-flight and reopen: no
packet replays and the ships show the saved state.

### F-SHIP-3

Give Ops a loadout and one minute of time today: Detail shows both rows.
A Node with neither shows neither row.

## Proposed contract changes

- [Center Map visual invariants](../system/center-map-ui.md#visual-invariants)
  gain two marker states. The current rule “no idle ring or glow” still
  holds; comms is an activity state and cloak changes stroke and plane
  opacity only. The fleet marker sheet needs an approved addendum.
