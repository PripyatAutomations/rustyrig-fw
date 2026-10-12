# Common subprocess API

`librustyaxe/subproc.h` is the single public subprocess header. Use `subproc_t`
and `subproc_*`; the former `rr_subproc.h` and `rr_subproc_*` names have been
removed before v1.0. Rebuild callers and update their includes and names.

The implementation is the existing line-oriented helper used by the callsign
lookup service. It owns one child and its pipes; it does not choose restart,
watchdog, logging or application protocol policy. The old slot-based supervisor
was compiled out and had no callable implementation; its declarations and dead
source have been removed.

| Operation | Behavior |
| --- | --- |
| `SUBPROC_INIT` | Initialize an unused handle with no child or open descriptors. |
| `subproc_spawn` | Create stdin/stdout pipes and fork/exec an explicit executable. |
| `subproc_spawn_fds` | Fork/exec with caller-owned descriptor mappings and child close list. |
| `subproc_write_line` | Write a line, append a newline and flush. |
| `subproc_read_line` | Wait for initial stdout readiness and read a line/chunk. |
| `subproc_reap` | Nonblocking wait for this child; return its waitpid status. |
| `subproc_close` | Close the pipes without signaling or reaping the child. |
| `subproc_stop` | Close pipes, signal, wait and escalate to SIGKILL if necessary. |

```c
#include <signal.h>
#include <librustyaxe/subproc.h>

subproc_t helper = SUBPROC_INIT;
const char *args[] = {"/usr/lib/example/helper", "--stdio", NULL};
if (subproc_spawn(&helper, args[0], args, true)) {
   char reply[512];
   if (subproc_write_line(&helper, "request") &&
       subproc_read_line(&helper, reply, sizeof(reply), 1000)) {
      /* Interpret reply in the application. */
   }
   subproc_stop(&helper, SIGTERM);
}
```

Boolean operations return true on success. Spawn success means a child and pipes
were created; an exec failure exits with status 127. `argv` includes argv[0] and
a terminating NULL. No shell expansion or PATH search occurs. Stop the current
child before reusing its handle. A successful reap clears the owned PID so later
cleanup cannot signal a recycled PID; pipe cleanup still requires close or stop.

Line reads preserve the existing stdio behavior: the timeout bounds the initial
readiness wait, not completion of an unterminated line. A zero timeout probes
readiness and -1 waits indefinitely. Buffer capacity can split long lines, as
with fgets. Helpers must write complete lines; applications needing arbitrary
binary or nonblocking framing can use `subproc_spawn_fds` with their own transport.
The host application owns SIGPIPE handling. If stderr is not merged into stdout,
it must drain `error_fd` to avoid filling that pipe.

`libfwdspmgr` uses `subproc_spawn_fds`, `subproc_reap` and `subproc_stop` for
worker lifecycle. It maps socketpairs to stdin/stdout/stderr and a packet-oriented
control socket to descriptor 3. DSP framing, codec policy and event-loop integration
remain in the manager. Modems and decoders can use the same mapping interface
without adapting binary protocols to line-oriented stdio. Browser clients have
no subprocess API to mirror.

```c
subproc_t worker = SUBPROC_INIT;
subproc_fd_t mappings[] = {{media_input, 0}, {media_output, 1}, {control_socket, 3}};
int child_close[] = {media_input, media_output, control_socket, parent_control_socket};
if (subproc_spawn_fds(&worker, args[0], args, mappings, 3, child_close, 4)) {
   /* Parent owns all descriptors; register them with its event loop. */
   subproc_stop(&worker, SIGTERM);
}
```

Mappings preserve sources even when source and target numbers overlap. Targets
must be distinct. The child close list never closes mapped targets; parent
descriptors stay caller-owned even after stop. Set FD_CLOEXEC on unrelated
application descriptors. Without mappings, the child inherits normal exec-safe
stdio. Reap only handles owned by your component, never `waitpid(-1)` in a shared
process: other components may have active children. Rebuild libfwdspmgr and its
callers together because its public worker now contains `subproc_t process`.

Run `bash librustyaxe/tests/test_subproc.sh` after building the local library.
The component test runner discovers it automatically. It exercises actual child
processes, multiple queued lines, merged/separate stderr, exit status, graceful
and escalated shutdown, close semantics, handle reuse and colliding descriptor
mappings. The DSP switching fixture also verifies manager reaping cannot consume
an unrelated helper's exit status.

Channel-bound DSP lifetime uses both local subscriptions and the protocol's
media-demand query for linked sessions. A remote-only listener must keep its
encoder active across cleanup sweeps; leaving and rejoining retain the normal
encoder pause/resume and hangtime behavior.
