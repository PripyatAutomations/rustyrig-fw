# RustyRig cluster state: owner-authoritative replication

Status: foundational design and first ephemeral consumer (native typing presence).
This is **not** a DHT or mesh implementation. Existing authenticated server
links and room-subscription paths continue to carry state.

## Invariants

1. **One authority per resource.** The server attached to a rig is the only
   writer for its catalog, CAT/PTT and authoritative room state. A mesh must
   never resolve simultaneous hardware writes by last-write-wins.
2. **Names are for people, UUIDs are for identities.** Diagnostic messages
   display `site [uuid]` where known. Routing keys and protocol identities
   stay canonical UUIDs.
3. **No global ordering assumption.** Each authority has an incarnation
   (new on restart) and monotonically increasing sequence values. Within a
   given incarnation, receivers ignore duplicate or older updates. Do not
   use wall-clock timestamps to decide which conflicting authority wins.
4. **State has a class.** Catalogs are durable-ish, owned descriptors
   synchronized with acknowledgments. Presence is ephemeral and has a
   maximum lease. Live radio state is transient but still owner-authored.
   Database history and replay must never include typing states.
5. **Partition means unavailable, not a second owner.** A missing authority
   cannot be silently replaced by an observer. Cached descriptors should
   become stale/unavailable until verified after reconnection.
6. **Subscriptions limit fanout.** Only branches with interested clients
   receive rapid room changes. Relay transport may change from tree to mesh
   without altering the state contract.
7. **Errors must be actionable.** State errors identify the node (name and
   UUID), affected resource, operation, reason and what action can restore
   service. Unknown names stay as UUIDs rather than guesses.

## First consumer: typing presence

Native rrprotocol `talk.typing` requests contain only the target room and
one of `started`, `active`, `paused`, `idle`. The room owner validates
the authenticated sender, room membership, chat permission and mute state.

The owner broadcasts `talk.typing`, `talk.from`, `talk.room`,
`talk.epoch`, `talk.seq`, `talk.ttl`. The epoch is random per server
incarnation; the sequence increases within the incarnation. Clients scope
their short-lived leases to the connection, room and nickname. `idle`
removes a lease; dropped notifications expire automatically (currently
10 seconds). GTK shows typing and paused status in the roster; drafts are
never transmitted. TUI is deferred to the v1.x roster/dialog work and
WebUI to the 2.0 redesign.

GTK users can disable outgoing indicators with `ui.chat.typing=false`.
Incoming indicators remain visible. No draft text is transmitted.

This is currently **per-username presentation**, not a multi-session CRDT.
The next iteration should aggregate separate authenticated session IDs
without exposing auth tokens, and should fence old incarnations across
route changes.

## Next mesh milestones (in dependency order)

- **Versioned catalog deltas**: owner epoch, sequence, digest and per-owner
  high-water mark. Reconcile via snapshot/delta requests and anti-entropy.
  Existing catalog begin/end/ack records already measure counts and latency,
  but do not prove every mesh branch has converged.
- **Explicit ownership/availability view**: record a node's verified
  reachability and stale state separately from catalog presence. On a
  partition, deny control writes to an unverified owner and explain why.
- **Route-independent delivery**: authenticated origin, message ID,
  deduplication horizon, hop limit and loop prevention for redundant paths.
  Never weaken provenance validation when replacing the current tree.
- **Subscriber recovery**: reconnect should rejoin interested rooms,
  request current state and replace expired leases without replaying stale
  typing or transient UI events.
- **Observable sync dashboard**: per-neighbor and per-owner last confirmed
  ack, accepted/rejected counts, freshness, high-water mark and last
  actionable error. Distinguish queue drained, received, accepted and
  network-converged states.
- **Only then evaluate DHT routing** if measured cluster size/topology
  warrants it. A DHT by itself does not solve ordering, ownership, or
  network partitions.

## Release regression checks

1. Two local GTK clients: started, typing, paused, idle and input clearing.
2. Simultaneous users in two rooms with the same nickname on different
   servers: no crossover.
3. Two-server relay: typing from both directions in a subscribed room;
   clients outside the room receive nothing.
4. Drop the link during active typing: the badge disappears on timeout,
   while authoritative rig control stays unavailable rather than falling
   back to another node.
5. Reconnect and restart the room owner: newly sequenced presence is
   accepted under the new epoch; old chat history contains no typing.
6. Run ASan/UBSan and GTK critical checks during rapid room switching,
   tab deletion, disconnect and GTK module unload.
