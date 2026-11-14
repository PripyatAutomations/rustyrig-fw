# GStreamer pipeline editor

The GTK client’s **Config → Edit Pipelines…** button opens the embedded
**Audio Routing → Pipelines** tab. The editor source is shared with the
optional standalone `rrpipelines` compatibility executable. It uses GStreamer’s installed element registry
and GObject property interfaces, so the toolbox and property panel reflect the
plugins available on that machine.

The editor displays each configured launch string as a connected element
graph. Drag blocks to arrange them; use the searchable toolbox to add elements,
then select source and destination blocks and choose **Connect**. **New
pipeline** creates an empty named graph that can be composed from the toolbox.
**Insert tee + queues** splits the selected element’s existing downstream
connections through a tee and a queue per branch. **Add tee branch…** adds a
queue and a chosen sink to a selected tee. The editor validates edited launch
strings with GStreamer before writing any JSON. The client then updates its
in-memory configuration; use **Save Config** to persist it.

The embedded editor receives a JSON configuration snapshot in memory and
calls the existing GTK Config validation and merge path on **Apply**.
This does not hot-swap active DSP worker pipelines; save settings and
reopen the relevant endpoint to apply the updated definition.
For compatibility, the standalone executable still accepts a JSON object
on stdin and writes one JSON object to stdout after **Apply**.
The `pipelines` member is an indexed object, not an array, so it round-trips
through librustyaxe’s dotted-key JSON API without changing identifier strings:

```json
{
  "pipelines": {
    "count": 1,
    "0": {
      "id": "pc16.rx",
      "launch": "appsrc name=rx-src ! appsink name=rx-sink"
    }
  }
}
```

`count` is the number of indexed entries. Each entry has a unique `id` and a
GStreamer `launch` string. Other object-valued envelope fields are preserved.
The flattened librustyaxe JSON API cannot round-trip arrays, so array-valued
fields are rejected. The client sends its configured pipeline set and accepts
added pipeline IDs.

GStreamer launch descriptions are a textual serialization of a graph, but
cannot faithfully represent every runtime pipeline object. Unmodified
pipelines keep their original launch text. Once a graph is changed, the editor
serializes the modeled elements, non-default writable properties, and links,
then reparses the result. Unsupported or invalid graph edits are rejected
instead of being returned as success.

## Audio Routing integration

The GTK Audio Routing floating workspace now has **Routing** and
**Pipelines** tabs. The Pipelines tab contains the actual in-process
GStreamer graph editor, element registry toolbox, property controls, link
editor, and Apply action. It no longer spawns `rrpipelines` from rrclient.

Double-click a pipeline-backed node in Routing to switch tabs and select
that configured pipeline. Double-click a GStreamer element in a mapped
pipeline to return to the owning Routing block. Not every element has
a corresponding logical RX block; unrelated elements are left in the
Pipelines view. The focus mapping currently covers client microphone
`src.client.dsp0` and stereo speaker `sink.client.mix`.

The editor is *in the GTK process*, but the DSP execution and audio
streaming remain in external fwdsp workers. Editing a configured launch
string is not equivalent to modifying a live GStreamer graph. Live
DSP topology snapshots, transactional hot-edit support, automatic AF
pipeline generation and stable element-to-logical-route identities
remain separate follow-up work. GTK builds compiled without GStreamer
show a clear unavailability message rather than requiring new dependencies
from the TUI or server.
