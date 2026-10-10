# Media adaptation and usage accounting

`librrprotocol` owns binary framing, sequence/timing observations, feedback and
payload counters. `librustyaxe` holds connection state. `rrclient` and `rrserver`
consume events and select DSP instances; `libfwdspmgr` carries quality/discontinuity
hints to `fwdsp`. Libraries do not render UI. These helpers can be reused by future
transports; this change implements neither WebTransport nor IRC+RTP.

## Congestion and quality

Audio uses per-connection, per-stream uint32 sequence numbers with wraparound.
Skipped outgoing frames consume sequence numbers; targeted codec-header replay
consumes only its recipient's sequence. Health observations use eight bounded LRU
slots; sequence counters survive observation-slot eviction. Duplicate/reordered
frames are ignored and gaps flag a decoder discontinuity. TCP itself does not
lose application frames: gaps currently indicate deliberate queue skipping.

The existing binary timestamp is the sender's monotonic microseconds at frame
creation. Compare *elapsed* sender time with elapsed arrival time, and keep the
best observed relative offset. This measures growth in delivery delay, not
absolute one-way latency. Correlated acknowledgements and ping replies supply RTT;
no NTP, WWV decoder or per-command timestamp extension is needed.

The quality controller combines:

* Outgoing audio backlog: quarter of the 8KiB bound requests 75%, half requests 50%.
* Smoothed RTT rising above the connection's minimum observed baseline. A naturally
  slow link is not penalized merely for having high RTT. Growth must last 500ms;
  thresholds are max(50ms, baseline/2) and max(150ms, baseline).
* Receiver feedback, scoped to an authenticated subscriber's current stream,
  direction, codec and room. Delay growth over 50ms or recent gaps requests 75%;
  sustained growth over 150ms requests 50%. Reports are limited to one per second
  per observed stream and expire at the sender after ten seconds.

Compact wire example (descriptive fields, no old-envelope fallback):

```json
{"op":"media.feedback","stream":1,"dir":0,"codec":"opus","quality":75,"late-us":80000,"gaps":1}
```

Quality drops promptly and recovers by 25 percentage points after each five healthy
seconds. It never exceeds the original pipeline setting. A shared server encoder
uses the lowest requested quality among its active subscribers; other subscribers
therefore receive that same encoding. A future per-recipient encoder is separate
work. Local quality events use the existing `media.quality-hint` helper.

Installed Opus pipelines use VBR and support live bitrate changes. Their default
complexity is already 10, the maximum, so there is no higher-complexity adjustment
to make. The DSP helper changes only properties marked mutable while PLAYING.
Installed FLAC compression and Vorbis quality remain fixed. FLAC is lossless:
compression level changes encoder effort, not audio fidelity, and cannot guarantee
that a stream will fit a smaller bandwidth allowance.

## Late/skipped sound

Codec setup packets use the reliable queue allowance instead of the small realtime
limit. Encoded chunks remain intact, including Ogg/FLAC parser input; sequence gaps
carry `GST_BUFFER_FLAG_DISCONT` through framed DSP IPC. Arbitrary loss is not
repairable for every format, and setup still fails if the reliable queue is full.
The real-codec regression tests preserve setup, skip a data packet, then deliver a
late burst through all eight installed base codecs.

Decoded canonical mono S16LE/16kHz PCM keeps the newest 100ms when a DSP receive
batch has accumulated. This is independent of encoded packet duration and does
not cut compressed packets. Browser playback caps scheduling at 250ms, bounds
WebCodecs decode queues, drops stale decoded output and cancels scheduled sound
on discontinuity/disconnect. Neither path can remove media already queued inside
an external hardware device or repair all damaged compressed streams.

## What the counters measure

Each connection tracks TX/RX text and binary **payload bytes and frame counts**
separately. TX means accepted into the transport send queue, not acknowledged
physical delivery. RX counts complete received text/binary frames before semantic
validation. WebSocket headers, TCP/TLS overhead and retransmissions are excluded.
IRC has text-only counters including line terminators; IRC accounting is local to
the native connection and does not create RustyRig server account records.

The server checkpoints authenticated user deltas every 60 seconds, on staff usage
queries/allowance changes, and at session close. Close writes a `session.usage`
AUDIT record with all eight session counters, session duration, TX seconds and
whether the final usage save succeeded. SQLite uses WAL to allow readers during
writes and a bounded 50ms wait for writer contention. Save failures are logged;
active connections retain unsaved checkpoints for retry. A process crash may lose
traffic since its last checkpoint; an unavailable database at final close is
reported rather than silently treated as a successful save.

Persistent totals combine all connections for a case-insensitive account since
its last reset. Session time is the sum of connected session seconds, including
concurrent connections; it is not a station reservation or an automatic money
charge. TX time comes from completed PTT records even with time-quota enforcement
disabled. A reset during active PTT counts only the portion after the reset.
Signed database counters saturate at INT64_MAX. Wire usage values are decimal
strings to preserve exact integers in JavaScript.

Only `admin|owner` requesters receive usage fields in `/whois`. Public account
information keeps its existing visibility. Both clients display bytes, frame
counts, TX time, session time and remaining bandwidth when included. `/whois`
retains its existing online-user lookup; `/quota BW SHOW` can inspect configured
offline accounts.

## Allowances and commands

Staff commands:

```text
/quota TX SHOW alice
/quota TX SET alice 30m
/quota TX ADD alice 2h
/quota TX RESET alice
/quota TX LIST
/quota BW SHOW alice
/quota BW SET alice 500
/quota BW ADD alice 2G
/quota BW RESET alice
/quota BW LIST
```

TX amounts keep existing dhms syntax. Omitting TX retains the existing time-quota
command syntax. TX RESET restores 60 minutes and clears recorded TX usage.

BW amounts are unsigned whole decimal **1M chunks**: `500` is `500M`, or
500,000,000 bytes. Accepted suffixes are M/G/T/P (case-insensitive), meaning
10^6/10^9/10^12/10^15 bytes. Fractions, signs, K, KiB and trailing text are rejected;
amounts must fit a signed 64-bit database integer. Multiple user/amount pairs are
accepted for SET/ADD, and multiple users for SHOW/RESET.

Unconfigured accounts have unlimited bandwidth. SET replaces the remaining
allowance without discarding measured usage; ADD tops it up. BW RESET clears byte,
frame, session-time and TX-time usage and restores `quota.bandwidth.default`
(default `1G`, registered in server defaults). It does not replace TX credits.
Active connection checkpoints are saved before reset so earlier traffic is never
charged again. Database-save failure prevents a bandwidth quota mutation.

Bandwidth exhaustion **warns only**. Negative remaining balances record overage;
chat/control, new radio transmissions and safety key-up remain available. Existing
TX-time enforcement remains governed by its existing configuration. These records
support station accounting and requests for project contributions, without adding
payments or mandatory bandwidth enforcement.
