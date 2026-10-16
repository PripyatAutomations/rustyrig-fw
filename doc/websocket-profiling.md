# WebSocket JSON parser profiling

Build with `USE_PROFILING` enabled to log incoming compact JSON operations at
INFO level under `profile`, alongside Event dispatch counts. Profiling is
process-local and runs in the shared RustyRig wire decoder, covering the server
and native clients without changing protocol messages or browser behavior.

The first decode produces an initial sample. Subsequent dumps occur at most
once every five minutes; the server's existing profile timer also dumps while
idle. Each cumulative dump lists the busiest 32 operations, ordered by received
message count. Rows contain:

- Incoming message count and JSON bytes.
- Rejected-message count, including invalid fields for a recognized operation.
- Total parser microseconds and integer average microseconds per message.

Parser time includes JSON parsing and validation/translation into the internal
dictionary. It excludes application-handler time, socket waiting, binary media
frames and the separate server-link envelope decoder. An invalid or unknown
operation uses a single bounded `<invalid/unknown>` bucket. Labels come from the
fixed operation registry; message contents, user names and credentials are never
included in these profiling rows.

Use counts and bytes to identify high-volume operations, then parser time to
judge their decoding cost. A frequent operation with little parser time may
still have an expensive application handler; use the event profiler or a CPU
profiler to investigate that path.

Counters use fixed storage and snapshots are sorted/logged outside the counter
lock. Logger callbacks can safely forward traffic or re-enter the dump function.
Without `USE_PROFILING`, the counters, clocks, locks and per-message profiling
work compile out; `rr_wire_profile_dump()` becomes a no-op.

# Dictionary cloning

`librustyaxe` provides `dict_clone(source)` for an owned dictionary copy without
serializing through JSON. It reuses `dict_merge()` and preserves scalar types,
including 64-bit integers, booleans, null and characters. Keys and string storage
are independently allocated, so either dictionary can be modified or freed.

`VAL_PTR` remains a borrowed pointer to the same caller-owned object. The clone
cannot infer its allocation, size or ownership; neither dictionary frees that
target. Callers needing an object deep copy must provide that object's own copy
operation. A null source or allocation failure returns null. Free a successful
clone with `dict_free()`.

Relay messages and native object-cache copies use this shared API. JSON remains
appropriate for actual transport and persisted bridge metadata. The manual
`librustyaxe/tests/bench_dict.c` benchmark compares cloning against a complete
JSON round trip without imposing hardware-dependent timing assertions.
