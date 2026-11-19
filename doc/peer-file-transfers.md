# Peer-to-peer and room file delivery

Status: **protocol/security design only**. The existing share browser exchanges
files with a server. Direct connections now have a binary FILE frame path, but
**a user-to-user or room transfer must not be offered as working in the UI**
until the consent broker, mesh relay and recipient tests below are implemented.

## Transport

Use native `rr_transport_send_binary_message()` and v2
`RR_BINFRAME_SUBSYS_FILE` (codec `fl01`). The current file payload contract
is a UUID, network-order offset, FIN flag and up to 24 KiB of bytes; it is
deliberately independent of filenames and recipients. Keep the existing
base64 implementation for older clients and future text-based modem transports.

## Offer and consent

1. The sender picks a file and exactly one target: an authenticated
   `user@site`, or a named room the sender has joined. The client publishes
   an offer with a fresh UUID, basename, byte size, expected SHA-256, target
   and room (if any). It never transmits file contents with the offer.
2. The owning/relay server verifies the sender's authenticated identity,
   limits outstanding offers, checks that the target exists, and snapshots
   eligible recipients from the **authoritative room owner**, never from
   untrusted client-supplied lists. Deduplicate sessions/users.
3. Each eligible recipient sees a GTK prompt with filename, size, sender,
   origin, destination and **Accept / Decline**. Acceptance must be an
   authenticated, explicit action for this transfer UUID. Default is decline;
   no implicit acceptance by joining a room or being an admin.
4. Keep the offer open for **30 seconds** measured by a monotonic clock.
   Recipients may accept or decline during the window. Do **not** send any
   file content before the window closes. A new offer is necessary for a
   late recipient.
5. At expiry, transmit only to the accepted recipient set. If none accepted,
   cancel without sending. Each receiver chooses a local destination and
   receives into a mode-0600 temporary file. Publish only after full size
   and SHA-256 verification.
6. A room “multicast” is **one encrypted copy per necessary mesh link**, not
   unauthenticated LAN/IP multicast or unfiltered room broadcast. A relay
   forwards only when its downstream branch includes accepted recipients,
   with a bounded queue, TTL and backpressure. Never forward arbitrary
   payloads from a room member.
7. Revalidate the sender and recipient identity, room membership, permissions
   and offer state throughout a transfer. Disconnect, leave, decline,
   revocation, size mismatch or expiry cancels the affected recipient.
   Never disclose recipient filesystem paths to the sender or other peers.

The server must retain a bounded offer table keyed by sender identity + UUID,
and an independently bounded recipient set keyed by authenticated principal
identity and session. Signed relay envelopes must bind offer UUID, sender,
target/room, hash, size and acceptance. Hop-decremented TTL remains outside
the signed content. The receiver must reject replayed, unoffered, duplicate,
out-of-order, unaccepted and oversized file chunks.

## Tests required before enabling peer/room sends

- Local user acceptance, decline, timeout at 30 seconds, late acceptance and
  cancellation; no bytes received before the window closes
- Multi-user room with two accepts and one decline, including a late joiner
- Three-node mesh where only accepted downstream branches receive bytes
- Invalid source identity, forged acceptance, substituted recipient and
  replayed signed offer/accept denied
- Recipient leaves a room mid-transfer, source disconnect, destination
  revocation, backpressure, partial write and server restart
- Exact byte count and SHA-256 checks before atomic rename; never write
  unaccepted file bytes or traverse recipient directories

Keep the opt-in protocol disabled until these tests pass. The lightweight
`rr_file_offer_accepts()` deadline predicate and its unit test currently
cover **only** the 30-second window calculation, not the broker or consent UI.
