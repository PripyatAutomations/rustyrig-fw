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

**Copy URL** copies the published endpoint. **Copy bridge config** is available
only for API-eligible entries with a URL the current bridge driver supports.
It creates a configuration snippet with published frequency coverage as offline
hints; paste it into rrserver's configuration and explicitly add its instance.
It does not configure a remote server, start a bridge, or open an audio stream.
Existing demand-driven bridge activation remains authoritative.

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
| `bridge.use` | Join/use station-configured receiver bridges. |
| `bridge.admin` | Bridge administration entitlement; also permits configured bridge use. It does not grant general station administration. |
| `kiwi.connect` | Entitlement for future selection of arbitrary listed API-permitted Kiwis; independent of private configured bridge use and bridge administration. |

Owners/general administrators retain bridge access. `bridge.admin`,
`kiwi.connect`, and wildcard grants covering them are owner-controlled through
existing account administration. For example:
`/user privs alice add bridge.use`.

Configured bridge permission is enforced at room JOIN and at media membership,
subscription, codec selection, demand and fanout, including direct RTP. Existing
sessions lose media access when their bridge entitlement is removed. Neither
privilege grants nor directory selection override operator API restrictions.

This browser does not yet expose remote bridge creation/editing or an arbitrary
public-Kiwi connection request. The separate administration/public-selection
entitlements provide the policy boundary for those future server operations;
they do not authorize clients to submit unchecked endpoints to rrserver.
