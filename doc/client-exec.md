# Native client shell commands

`/exec <command>` runs `/bin/sh -c <command>` asynchronously and prints stdout
in the originating tab. Shell quoting, pipes, redirection and expansions are
preserved. GTK and TUI use the same implementation; this is local to rrclient,
with no server command or browser equivalent.

| Form | Output |
|---|---|
| `/exec date` | Show output locally |
| `/exec -o uptime` | Send stdout lines as ordinary chat to the originating tab |
| `/exec -e printf '/rxroute L\n'` | Pass stdout lines through the normal client input parser |
| `/exec` | Show usage and running job IDs |
| `/exec -stop` | Stop all running exec jobs |
| `/exec -stop <id>` | Stop one job |
| `/exec -stop all` | Explicitly stop all jobs |

`-o` sends literal text, so an output line beginning with `/` remains chat.
`-e` interprets commands and ordinary text exactly through the shared input
parser. These modes are mutually exclusive. Both retain the originating server
and conversation, without moving focus or filling input history. Forwarding
stops when the originating session disconnects, including across reconnects.
Stderr always remains local; it is never sent as chat or interpreted as input.

There may be four concurrent jobs. Forwarded output is paced to at most four
lines per second per job, with pipe backpressure. Local output is drained in
bounded batches so networking/audio can continue. Partial lines wait for a
newline or EOF; CRLF is normalized. Binary lines and lines exceeding 16 KiB are
skipped whole, never executed as truncated commands. Stdin is closed, so these
are noninteractive jobs. Active process groups are stopped and reaped on job
cancellation or client exit. No exec polling runs when there are no jobs.
