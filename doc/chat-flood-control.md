# Chat flood control

RustyRig applies two independent layers of outbound chat rate control. The GTK/TUI client's send queue smooths bursts (including messages sent to IRC), while each RustyRig server enforces its own limits for ordinary public chat, actions and private messages. Server checks cannot be bypassed by disabling the client queue.

## Server settings

Place these options under `[general]` in **that server's** `rrserver.cfg`. Every server can choose its own limits.

```ini
[general]
chat.flood.burst=5
chat.flood.interval-ms=2000
chat.flood.shun-seconds=10
```

- `chat.flood.burst`: maximum messages allowed in an immediate burst (1–100; default 5).
- `chat.flood.interval-ms`: interval in milliseconds to replenish one message token (100–60000; default 2000, or 1 message per 2 seconds sustained).
- `chat.flood.shun-seconds`: chat-only shun applied if a user exceeds the available tokens (1–3600; default 10).

Limits are per authenticated account, shared across rooms. On excess, the server blocks further ordinary chat until the shun expires and sends a notice. Shunning does **not** disconnect the user or prevent CAT/audio traffic. File chunks are not counted as regular chat; other command traffic is not yet covered. Limits are currently clamped to the ranges above; no rate-limit disable switch is implemented.

An account with the global `chat-flood` permission bypasses the server check. A room-scoped `chat-flood` resource grant exempts messages directed to that room, using the existing privilege system. Avoid granting this exemption widely.

## Client settings

Place these in the client's `rrclient.cfg`, under `[general]`:

```ini
[general]
chat.send.burst=4
chat.send.interval-ms=2000
chat.send.queue-limit=64
```

- `chat.send.burst`: immediate outgoing chat burst (1–100; default 4).
- `chat.send.interval-ms`: time to regain a send token (100–60000 ms; default 2000).
- `chat.send.queue-limit`: maximum pending chat messages (1–1000; default 64).

The client queue retains each message's original server and destination. Messages pending for a disconnected or replaced session are discarded rather than sent to another server. The limit applies to ordinary text sends using both IRC and RustyRig connections, not every slash command or automatic transmission.

## Operational notes

Defaults are intentionally conservative to suit everyday chat, while preventing paste floods. Keep the client burst at or below the server burst for normal users. Inspect `ws.chat` warnings when diagnosing server shuns. Since independent IRC networks set their own limits, lower the client rate for stricter networks.

Changes to config should be picked up when the relevant process reloads the updated configuration; restarting the client or server ensures that default and runtime values are refreshed.
