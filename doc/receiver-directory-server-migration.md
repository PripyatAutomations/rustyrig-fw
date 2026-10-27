# Server-owned receiver directory (migration contract)

The SDR Directory is a **catalog of external receiver metadata**, not a collection of
live rrserver rigs. Do not insert thousands of directory entries into the live
object inventory or spawn bridges until a user requests a connection.

## Authority and storage

- `rrserver` owns HTTPS mirror fetches, validation, four-hour caching, and
  SQLite snapshots (default under the server's data directory, not the UI
  process cache). Never retrieve directly from kiwisdr.com; use only an
  explicitly configured permitted mirror.
- Keep `receiver_records`, `receiver_details`, `receiver_state`, and
  `directory_cache` semantics, including 90-day retained tombstones and
  operator API restrictions.
- A single network authority is elected/configured for linked servers.
  Other servers cache signed/versioned snapshots from that authority; absence
  of a route must not prevent using a previously validated local copy.
- No per-receiver HTTP probe is triggered by browsing, filtering, or a cache
  refresh. An actual connection remains subject to websdr permissions.

## Wire contract

- New directory namespace: `receiver-directory.request` with `list`,
  `status`, and `refresh` operations. The selected authenticated server
  answers independently of the active room.
- Snapshot response includes `source-node`, monotonic generation, fetched
  timestamp, not-before refresh deadline, schema, record count, and content
  digest. Send records in bounded, sequenced chunks plus a final checksum,
  not one unbounded WebSocket frame. A client applies the complete snapshot
  atomically; partial data must not overwrite its last good cache.
- A normal request returns the cached generation. Forced refresh is explicit
  and permission/rate-limited; the server never lets each client turn a
  four-hour cache into a mirror-request storm.
- Server links advertise the generation/digest once and transfer snapshot
  data on demand, rather than distributing thousands of entries through
  `server.inventory.catalog` every time a peer connects.
- Publish only the normal receiver directory metadata and its provenance.
  Local unreachable/restricted reports are *private to the reporting user*
  until an explicit moderated sharing policy exists.

## Frontends and migration

- GTK/TUI/Web all consume the same server directory protocol. GTK retains
  the existing table and search UI; moving data ownership does not solve
  GtkTreeModel row population costs, so keep the bulk-sort optimization.
- Retain client SQLite for offline operation and migration; client direct HTTP
  fetching is retired **only after** server snapshots are received and
  validated end-to-end. A mixed-version client/server remains functional.
- Linked SDRs that are genuinely attached continue appearing in Resource
  Explorer as rig/VFO objects. Directory listings are browsed in the SDR
  Directory, not invented rig nodes.
- Cover cache expiry across restart, bad JSON, incomplete snapshot, duplicate
  URL, source change, lost server link, no permission, and forced refresh in
  component tests before removing the existing client fetcher.
