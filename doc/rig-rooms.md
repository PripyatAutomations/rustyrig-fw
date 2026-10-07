# Site lobby and rig rooms

Authenticated sessions join `#<station.name>` as a chat-only site lobby.
A rig room is opt-in: `/join #rplywv00-rig0` attaches to that radio's room.
`/part` releases that room's media subscriptions. The site lobby remains joined.

Each configured rig owns one authoritative TX room: `#<station.name>-rigN`.
Only server initialization creates these base rooms. The per-rig `room` setting
must match that canonical name; other rooms are chat-only unless they are
mapped RX subrooms beneath a configured rig's namespace.

```ini
[general]
station.name=rplywv00
rig.instances=rig0 rig1
rig.default=rig0

[rig:rig0]
backend=internal
vfos=A B
room=#rplywv00-rig0
rx-independent-vfos=B

[rig:rig1]
backend=hamlib
hamlib.model=2
hamlib.device=127.0.0.1:4532
vfos=A B
room=#rplywv00-rig1
```

An administrator can create `#rplywv00-rig0.monitor` and bind its VFOs with
`/room #rplywv00-rig0.monitor vfo add rig0.vfo_b`. Bindings must belong to that
room's rig. These rooms receive RX audio and VFO status but cannot transmit or
change mode or width. Frequency changes are permitted only for mapped VFOs
explicitly declared safe to tune without moving the shared LO.

`rx-independent-vfos` lists native VFO letters separated by whitespace or
commas. It overrides `rx-independent-tuning`, which allows all configured VFOs
when true. Both default to no permission. Set these capabilities according to
the backend and hardware: the server does not infer LO independence from a
backend name. An invalid VFO list aborts initialization.

The server persists actual VFO UUID bindings and restores valid RX subrooms at
startup. Invalid or cross-rig bindings are removed; ordinary rooms retain chat
history but receive no VFO controls. Configured base bindings are rebuilt from
rig configuration and cannot be edited or removed with `/room`.

CAT requests carry their room and target its rig. Both CAT and UUID property
writes require appropriate room membership and permissions. Leaving the TX
room releases that session's transmission. Existing station-wide PTT arbitration
and interlocks remain in force; this does not enable simultaneous independent TX.

GTK's main controls follow the selected base rig room. Clicking a VFO row in a
docked userlist selects that room and VFO; detached rows do not change selection.
RX subrooms offer a frequency Tune button only for permitted VFOs. The active
row stays highlighted, including in detached panels. Its default dark red
background is configurable with `#room-vfo-row.room-vfo-active` in GTK CSS
(escape the initial `#` as `\#` in the configuration file).

## Audio

Every configured rig VFO gets independent RX and TX channel UUIDs.
`media.available` adds `media.room`, `media.control-room`, `media.joined`, `media.rig-uuid`, and
`media.vfo-uuid`. `media.joined` reflects the receiving session's membership,
not whether some other user has joined. RX channels can be presented in a joined
subroom with a matching VFO binding; `media.control-room` identifies their primary
rig room. TX channels require membership in that primary room. Unscoped channels retain their existing
subscription behavior.

Native and browser clients automatically attach the rig's shared audio pair or the active VFO's configured pair
in a joined rig room. They have one local audio pair: joining another rig room
or selecting its chat tab switches the automatic pair; ordinary chat tabs do
not change it. Manual `/media` subscriptions remain possible within joined
rooms. NONE continues to disable the selected audio direction. The server
rejects subscriptions and codec changes outside the channel's room, removes
subscriptions on PART, and checks membership during media delivery.


### Shared and per-VFO audio

Each `[rig:<alias>]` defaults to one shared RX channel and one shared TX
channel, named `<alias>.rx` and `<alias>.tx`. Both announce `media.vfo=255`
(no specific VFO), their rig UUID, and no VFO UUID. Selecting A/B changes
controls without replacing the audio subscription. VFO state still comes
from that rig's VFO objects.

```ini
[rig:rig0]
vfos=A B
audio.per-vfo=false

[rig:rig1]
vfos=A B
; Dual RX, one shared transmitter:
audio.rx.per-vfo=true
audio.tx.per-vfo=false
```

`audio.per-vfo=true` opts both directions into per-VFO channels.
`audio.rx.per-vfo` and `audio.tx.per-vfo` override their direction independently
and otherwise inherit `audio.per-vfo`. A/B with shared audio exposes two
channels; independent RX and shared TX exposes three; both directions per-VFO
exposes four. Only backend-supported configured VFOs are provisioned, within
existing channel limits. Configure independent directions only where the
station supplies independent hardware audio; this setting does not create
additional receivers or transmitters. Existing rigs needing per-VFO channel
names must explicitly opt in. Changes take effect on server startup.

RX subrooms may subscribe to their rig's shared receiver; TX remains restricted
to the rig's base room and actual PTT holder. Sharing audio does not grant
independent tuning or TX authority to an RX subroom. Codec changes apply to the
shared channel for all its subscribers. Always-record settings on any exposed
VFO arm that rig's shared RX recorder; TX recordings retain the actual keyed
VFO's recording/log identity.



PCM endpoints default to `[pipelines] src.<alias>` and `sink.<alias>`.
The existing `src.rig0`/`sink.rig0` pipelines still serve rig0. Define dedicated
pipelines for rig1's input/output devices. Hamlib controls the rig; NET rigctl
does not itself supply a local audio capture/playback device. An absent rig1
pipeline leaves that endpoint unavailable and never uses rig0's device.

Endpoint names may be overridden per rig:

```ini
[rig:rig1]
audio.source=src.radio1
audio.sink=sink.radio1
```

## Autojoin

The native client already supports a per-server list in `config/rrclient.cfg`:

```ini
[server:my-station]
autojoin=#rplywv00-rig0,#rplywv00-rig1
```

Use the existing server section's other connection settings. Lists accept
commas or whitespace. The browser's **cfg** tab has a “Rooms to join after
login” setting, saved for that site's browser origin. An empty list leaves the
session in the site lobby until the operator joins a rig room.

## Room cleanup

Any authenticated account can create an undashed room with `/room add #name`
or by joining an unknown undashed room. Any dash reserves the name for station
scoping, including future sites (`#rplywv00-*`, `#nycnc04-*`); creating these
requires `admin|owner` or server provisioning. The configured site lobby is also
protected. Restoring deleted rooms, removing rooms and editing VFO bindings
require `admin|owner`. Other users can join existing active rooms.

`/room remove #name` replies with `To confirm, please use /room remove #name abcd13`.
The six-digit hex token is valid for five minutes, once, in the issuing session,
for the exact room and removal options. The legacy `/room #name remove [token]`
syntax requires the same confirmation. Site and configured base rig rooms are protected.

Plain removal sets `rooms.deleted=1`, hides the room from listings and rejects JOIN.
Topic, VFO bindings and chat history remain available for restoration with
`/room add #name`. `--force` (`-f`) permanently removes the room record and bindings,
keeping chat history; `--force --history` (`-f -h`) also deletes that room's chat
history. Include the same options when confirming. `--history` requires `--force`.
PTT logs and audio recordings belong to the rig and remain intact.

`audit_log` records `room.created`, `room.removed` and `room.restored`, with the
responsible username (or `server` for provisioning), timestamp and room name.
Permanent removals record their options. Audit insertion and each room change
share a transaction; failed audit writes roll back the change.
