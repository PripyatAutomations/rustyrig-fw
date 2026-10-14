# Media connection notices and readable diagnostics

Native room chat shows one connection result per RX/TX stream, plus failure or
fallback reasons. Failure notices identify the failed transport and the next
route separately, for example `direct RTP unavailable: server supplied no RTP
endpoint; falling back to proxy RTP`. Routine requesting/connecting stages go
to debug logs.
Codec-selection confirmations appear in the selected channel's room, including
when initiated from a global widget or status tab.

Media diagnostics use an owner/rig/VFO/direction path such as
`rplywv00.rig0.vfo-a.tx` and include the codec and UUID. Shared rig audio omits
the VFO segment (`rplywv00.rig0.tx`). Browser notices use the same readable
paths. These are display labels; UUIDs remain the wire addresses. Missing
metadata is shown as unknown until discovery arrives.

Server pipeline and recording logs prefix stream names with the local station
and include room, codec and UUID. Protocol channel logs include symbolic names
and room context without assuming the hosting server's configuration.
