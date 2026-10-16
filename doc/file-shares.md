# Exported file shares

`/files` opens **Server Files** in GTK and the browser. GTK Resource Explorer
also has a **Files…** button. The native window selects among authenticated
RustyRig server connections; IRC has no server filesystem. TUI `/files [path]`
lists an exported directory as text.

The namespace begins at `/` and contains only configured shares. Nothing in the
server filesystem is exported automatically. For example:

```ini
[share]
name=recordings
path=/var/lib/rustyrig/recordings
read=files.read
write=files.write

[share:logs]
path=/var/log/rustyrig
read=files.read
write=none

[share:configs]
path=/etc/rustyrig
read=files.read
write=files.write
```

The first form supports repeated `[share]` sections, with `name` before the
other settings. `[share:<name>]` is equivalent. Share names must be one path
component without dots. A share may set `enabled=false` to withdraw it.
The configured physical directory must already exist and be accessible to the
server's OS account. Configured paths are never sent to clients.

Grant `files.read` and/or `files.write` in the user privilege editor. Admin and
owner flags do not implicitly grant file access. Each share may instead name
its own privilege expression, such as `read=recordings.listen|files.read`;
`write=none` always denies writes. These grants are independent of OS
permissions, which still apply. The root lists only shares for which the
account has read or write access. Every operation rechecks current permissions;
external SQLite revocations apply before the next command.

Expand directories to fetch their contents. Siblings sort naturally, with
folders first. Read/write indicators describe your access. Upload and New
directory act on the selected directory. Download and Edit act on a selected
file. Rename changes its name; Move accepts a full destination in the virtual
namespace, for example `/recordings/archive/session.ogg`. Delete removes a file
or empty directory, with confirmation. Export roots cannot be renamed or
deleted. Rename and Move never replace an existing destination. Moves use an
atomic filesystem rename and report an error across different filesystems.

Uploads use 24 KiB chunks and hidden temporary files in the destination
directory. Two bounded workers flush completed uploads without blocking the radio loop.
The main loop rechecks live permissions and the destination before atomic
publication. Disconnect, cancel,
invalid data and write errors discard the temporary upload. New uploads do not
overwrite existing files. Remote editor saves explicitly replace the selected
file and preserve its permission bits. Successful writes, mkdir, rename/move
and delete generate `files` audit events with the account and virtual paths.
Symlinks and non-regular files are neither browsable nor readable. Paths are
resolved component by component using directory descriptors and `O_NOFOLLOW`;
`..`, `.` and ambiguous separators are rejected.

GTK Edit reuses the configuration text editor, with server and virtual path in
its title. Save uploads the edited text; it does not reload or apply the file
as the local client's configuration. Server configuration edits similarly do
not automatically rehash the server. The browser provides an equivalent text
editor. Editing requires writable UTF-8 text without NUL bytes, up to 1 MiB.

Limits: one active upload per server connection, 128 concurrent server uploads,
10,000 entries per directory listing, and 30 seconds without transfer progress
before client timeout. `files.max-size` in `[general]` defaults to 1 GiB and
limits uploaded bytes. Browser downloads stream to disk when the File System
Access API is available; the Blob fallback limits downloads to 256 MiB. Native
downloads stream to a temporary local file and publish it on completion.

All registered media subsystems, including webcam/video and file streams,
appear in discovery. A stream uses its registered VFO or rig as parent, falling
back to the site if that parent is unavailable. Shares also appear in Resource
Explorer. Linked peers advertise share metadata with access expressions;
filesystem operations require a direct connection to the owning server.

## Protocol ownership

`librrprotocol/wire-schema.json` defines the additive `file.*` wire operations;
there is no filesystem implementation in the protocol library. The library
emits `file.request`; rrserver listens and owns namespace resolution,
authorization and I/O. Replies use correlated `request.id` values.

- `file.list`: `path`; emits `file.entry` rows (`name`, `kind`, `size`,
  `readable`, `writable`) and `file.end`.
- `file.read`: `path`, `offset`; replies with `file.data` containing canonical
  base64 `data`, `offset`, total `size` and `eof`.
- `file.begin`: `path`, optional explicit `overwrite`; `file.chunk`: `path`,
  sequential `offset`, canonical base64 `data`; `file.commit` / `file.cancel`:
  `path`. Each command acknowledges with `file.result`.
- `file.mkdir`, `file.delete`: `path`; `file.rename`: `path`, `dest`.
- `file.result`: empty `error` means success; nonempty `error` is a visible
  failure. Internal host paths, passwords and file contents are not audit data.

The native common client owns transfer state and local files in
`rrclient/files.c`; GTK owns only widgets. The browser implementation mirrors
those semantics in `www/js/webui.files.js`.

## Storage latency

Directory traversal and bounded chunk reads/writes still run in the server
request handler (`rrserver/files.c`, `list` / `request`). File flushes run in a
two-worker pool; publication runs on `server.poll` after a fresh authorization
check. Slow/network filesystems can still delay individual metadata or chunk
operations. A fully asynchronous storage backend is separate future work.

## Default test shares

The supplied server configuration exports `/logs` from `/var/log/rustyrig`
read-only, and `/shared` from `/home/rustyrig/SHARED` read/write. Debian setup
creates the shared directory if missing without changing an existing directory.
For an existing installation, run `sudo tools/setup-test-shares.sh` (optionally
pass your server config path), then restart rrserver. The script keeps a backup
and preserves other configuration. Grant `files.read,files.write` explicitly to
your test account and use `/files` or **Files…** to browse and upload.
