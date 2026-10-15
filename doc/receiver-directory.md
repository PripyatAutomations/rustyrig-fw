# Receiver Directory

Press **Alt-Z** in GTK to open the Receiver Directory, or use
`/receivers`. KiwiSDR is the first provider; the directory, UI, cache and
configuration use generic receiver names. Other WebSDR types need explicit
provider parsers and access rules; they are not silently treated as KiwiSDRs.

The dialog searches name, location, host, grid, contact and hardware. It shows
published users/capacity, API allowance, hardware/limits, antenna, frequency
coverage, available contact information, additional properties and observation
times. Contact information is often absent from the directory; that is shown
as **Not published**, without fetching a receiver's web page or `/status`.

**Accessible only** hides directory-reported offline/flagged/missing entries,
web-only or unknown API policies, and local unreachable/restricted reports.
This means “no known obstacle”, not “tested reachable” or a guarantee that a
channel is available. Disable it to inspect retained entries and their reasons.
**Mark unreachable**, **Mark restricted**, and **Clear report** save local
observations; they do not probe the receiver. Reports remain separate from the
published offline flag and external API policy. Clearing a report cannot
permit a receiver whose operator disabled external API access.

**Connect** requests the selected endpoint through the currently selected,
authenticated RustyRig server. The server reuses an existing matching bridge or
creates a temporary RX-only rig, then the client joins its room. Creating the
rig does not contact the receiver; joining/subscribing supplies demand. Leaving
the room or disconnecting releases the receiver when nobody else needs it.
Temporary rigs expire after sixty seconds without demand, with a default cap of
eight (`websdr.max-temporary`). `websdr.idle-timeout` sets unused rig lifetime
in seconds (1–3600, default 60). Idle source connections are released promptly;
this timeout controls removal of the temporary rig descriptor. Room history
remains ordinary chat history.

**Add to server config** requires `websdr.admin`. The server appends the validated
bridge section and roster entry atomically to its main configuration file.
The receiver is then available to `websdr.listen` users, including after restart.
Saving does not join the room or open audio. **Copy URL** and **Copy bridge
config** remain available for manual configuration; directory coverage fields
are hints, while the connected driver's capabilities remain authoritative.

TUI users can run `/receivers connect <URL>` or `/receivers add <URL>` using a
listed, API-permitted endpoint or an explicit URL outside the cache. Known
directory restrictions remain enforced. The browser supports explicit URL actions
through its connected server, without a local directory cache or import dialog.
Only Kiwi HTTP endpoints are currently supported; endpoint parsing belongs to
the driver provider, while privileges and lifecycle use generic WebSDR names.

## Fetching and caching

The temporary default is AetherSDR’s JSON mirror. Replace it with our own
mirror before release, or set the URL empty to disable fetching:

```
# [general], or /set keys
receiver-directory.url = https://cdn.aethersdr.com/kiwi.json
receiver-directory.cache =
```

The URL must be a permitted HTTPS JSON mirror. Plain HTTP is accepted only
for loopback fixtures. Direct requests to `kiwisdr.com` and its subdomains are
refused; there is no origin fallback, redirect following, browser impersonation,
CAPTCHA handling, receiver scanning, image loading or background polling.
Fetching begins only on an explicit browser open or refresh. Importing a JSON
file is available through the dialog or `/receivers import <file>`; TUI users
can use `/receivers refresh` and `/receivers` to fetch and list eligible rows.

`RRCLIENT_RECEIVER_DIRECTORY_URL` overrides the URL for development without
changing shipped configuration. A temporary development environment file and
a downloaded snapshot may be kept under ignored `run/`; neither is installed.
The temporary source is the documented AetherSDR JSON CDN. RustyRig still
needs its own permitted mirror before release.

KiwiSDR's maintainer asked AetherSDR to mirror its directory to reduce aggregate
client traffic; see their [integration notes](https://github.com/aethersdr/AetherSDR/blob/main/docs/kiwisdr-public-directory.md)
and [schema-1 contract](https://github.com/aethersdr/AetherSDR/blob/main/docs/kiwi-json-schema.md).
RustyRig implements that published JSON shape independently, with an optional
`provider: "kiwi"` discriminator. Unknown providers/schemas are refused.
It does not scrape the origin's HTML. Operator `ext_api` zero and missing policy
remain distinct, and neither is offered for API bridge configuration.

One worker fetches/parses metadata without blocking the GTK/audio loop. Requests
identify as `RustyRig/<version> ReceiverDirectory`, use certificate verification,
allow compression, and cap decoded bodies at 8 MiB and lists at 10,000 entries.
There is one in-flight request and at least one hour between attempts, including
manual refresh and restarts. Longer Cache-Control/Retry-After delays are honored
up to seven days. ETag/Last-Modified support conditional GET. Invalid responses,
429s, unavailable mirrors and unexpected redirects retain the last good list.
No request is sent to any receiver by directory browsing or filtering.

The private SQLite cache defaults to
`$XDG_CACHE_HOME/rustyrig/receivers.sqlite3` (normally
`~/.cache/rustyrig/receivers.sqlite3`). `receiver-directory.cache` overrides it.
`directory_cache` holds the last payload/validators and persisted retry deadline;
`receiver_records` holds canonical URLs, provider identity, original properties, `last_seen`,
`deleted` (missing from the directory) and `offline` flags. `receiver_state`
holds local failure/restriction reports, reasons and timestamps. Endpoint URLs
with and without a root slash/default port share a record.

## Learned metadata and retirement

`receiver_details` stores nonempty fields individually with `directory` or
`live` provenance and an observation time. Directory updates cannot replace
live learned fields; blank fields never erase earlier information. Older live
observations cannot replace newer live observations. A successful connection
can fill missing name, contact, location, antenna, hardware, grid, coverage and
published external API allowance, when the receiver supplies those fields.

The Kiwi driver extracts an allowlist from the normal `load_cfg` message.
Passwords and unrelated configuration are never copied. Versioned local bridge
IPC carries those fields; rrserver caches them using the same endpoint identity
as capability hints, and exposes read-only `receiver.*` rig properties. Native
clients coalesce those properties into the directory only after the rig has a
confirmed `receiver.connected_at`. Reopening/discovering a cached rig needs no
receiver connection. Metadata received through server links uses the same object
properties. Normal tuning updates do not rewrite the directory database.

Missing entries are retained with `deleted=1`; reported offline receivers remain
separate records. Their details and local reports survive list refresh, import,
restart and temporary directory outages. A valid directory update purges entries
that are still missing and were last seen over 90 days ago, including their
per-field metadata and local reports. A listing or confirmed live observation
refreshes last-seen. Offline entries still present in the directory are retained.
Refresh failure never retires or purges receivers.

## Station privileges

These are server account privilege strings, in addition to existing radio/media
permissions:

| Flag | Scope |
|---|---|
| `websdr.listen` | Listen to enabled receivers from the server's configured bridge roster. |
| `websdr.connect` | Connect to any supported endpoint through a temporary server rig; also permits configured receivers. |
| `websdr.admin` | Add a receiver to the server configuration; does not itself grant listening or ad hoc connection. |
| `bridge.use` / `bridge.admin` | Retained configured-bridge access compatibility. |

Owners/general administrators retain access. `websdr.connect`, `websdr.admin`,
`bridge.admin`, and wildcard grants covering them are owner-controlled through
existing account administration. For example:
`/user privs alice add websdr.listen`.
The provisional `kiwi.connect` flag is replaced by `websdr.connect`; existing
accounts using it need an explicit owner grant of the generic flag.

Configured bridge permission is enforced at room JOIN and at media membership,
subscription, codec selection, demand and fanout, including direct RTP. Existing
sessions lose media access when their bridge entitlement is removed. Directory selection respects published operator API restrictions; the receiver
continues to enforce its external API allowance for explicit connections.

This browser does not yet expose remote bridge creation/editing or an arbitrary
public-Kiwi connection request. The separate administration/public-selection
entitlements provide the policy boundary for those future server operations;
they do not authorize clients to submit unchecked endpoints to rrserver.
