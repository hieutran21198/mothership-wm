# Show ship status on the Center Map

**Status:** Cloak, its command packet and the Detail Silent running button implemented 2026-11-27 and owner accepted in a native Mothership Dev session (candidate `build-zvH5676i`) on 2026-11-27; the Detail Loadout and Today rows implemented with their features; Comms arcs implemented 2026-11-27 and owner accepted in a native Mothership Dev session (candidate `build-kuuGdEco`) on 2026-11-27; the music zone approved by the owner and implemented 2026-11-27, awaiting native acceptance.
Supports [Read each Node's state from the Center Map](../goals/show-ship-status.md).
Design: [Fleet Ops preview, scene 1](../system/fleet-ops-preview.html); music:
[Music zone preview](../system/music-zone-preview.html), variant B.

Status is paint on the world layers of the
[Center Map](../system/center-map-ui.md). It adds no hit targets and does
not change marker size, label position, route endpoints or the minimap.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Sample | Panel → PulseAudio | Only while the Map shows the fleet, run `pactl subscribe`; on sink-input events (coalesced for 150 ms) list sink inputs and resolve them to Nodes with the [Node Volume](../system/home-panel.md#node-volume) scope rules. A stream is playing when it is not corked. Shared or unidentifiable streams and the Default Node mark nothing. Stop the subscription and clear comms when the Map closes. |
| Comms | Panel | For comms streams, draw three short arcs ahead of the ship's bow in the marker cyan. With motion, brightness travels outward on a 1.2 s loop while the Map is shown; Motion Off shows static arcs. When every playing stream of the Node is muted (Node Volume mute or a Silent running hold), the arcs are static and struck through. Custom Node SVGs get the same arcs above the icon. The loop pauses with the route animation (focus loss, Motion Off); an occluded Map receives no frames. |
| Cloak | Panel | For a Node whose [Silent running](run-silent.md) preference is not Off, draw the hull outline dashed and the dark planes at about 50% opacity. The label, focus treatment and unread badge keep full contrast. |
| Command | Panel | The Node Detail **Silent running** button saves the change and Mothership sends the command packet at once; the button shows the saved state immediately and the ship changes when the packet arrives. A Silent running change made while the Map is closed waits for the next Map open. After the opening radar, Mothership sends one straight cyan command packet with a bright chevron head along that Node's route (about 550 ms). On arrival a cyan ring closes on the ship while the hull morphs between solid and cloaked (about 150 ms). Turning it off sends the same packet and morphs back. Until arrival the ship shows its previous state. Commands fly before notice pulses, one packet at a time. Toggling on and off before the Map opens sends nothing. With Motion Off, when Mothership or the ship is out of view, or if the Map closes mid-flight, the ship shows the saved state without a packet. Do not move the camera or recolor the route. |
| Classify | Panel → session bus | A playing stream is music when its `media.role` is `music`, or when its process or an ancestor owns an MPRIS player whose `PlaybackStatus` is `Playing`. Players are read with `busctl` from the Mothership app bus that launched apps use (the user bus only when there is none), and only when an untagged stream is playing; without busctl or a bus only the tag counts. Every other playing stream is comms. A Node with both shows the zone and the bow arcs; each kind is muted only when all its streams are. |
| Zone | Panel | Paint a fixed regular octagon (45° chamfers) of radius 52 logical px at 1× around the ship, scaled with icon zoom: a faint cyan tint that follows the bass, a slowly drifting dashed edge and static vertex marks. Its own layer sits above the radar and under Mothership, routes, notices, ships, labels and badges. It keeps 3 px clear of its Node's name and badge, adds no hit target and clips to the Map content. Overlapping zones add; each tint stays below 10%. |
| Waves | Panel | On each half beat a wave grows from 19 px to the zone edge in 1.5 s, fading in as it leaves the hull, fading out toward the edge and trailing a thinner, dimmer echo. Downbeats are brighter, alternate off-beats faint. The beat is procedural (104–120 BPM and a phase per Node); no audio is read. |
| Halo | Panel | 32 radial ticks from 17 px around the hull show the same procedural spectrum: low bands at the stern, high at the bow, mirrored left and right. |
| Music states | Panel | Muted: the faint edge, still halo and a strike at one corner; no waves. Paused or stopped: the zone goes with the next sample. Motion Off or focus loss: two still waves and a static halo. Hidden during the radar, Arcade and Connect relay. The tick runs only while a music Node plays and the Map is shown. |
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

### F-SHIP-4

Play music in Studio and open the Map: Studio shows the zone, waves and
halo; no comms arcs. Start a call in Ops: Ops keeps the arcs. Put Studio in
a call too: it shows both. Pan Ops to the zone edge: Ops and its label stay
readable and a click on Ops still selects it; a click inside the zone on
blank space still pans. Zoom in and out: the zone scales with the icon and
the Studio label stays clear. Mute, pause, Motion Off, close and reopen the
Map: each state matches the Music states step and no subscription remains
after close. Repeat with a player that sets no `media.role` (a browser
tab with MPRIS) to prove the bus fallback.
