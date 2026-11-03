# GStreamer pipeline editor

The GTK client’s **Config → Edit Pipelines…** button opens `rrpipelines`, a
standalone GTK3 graph editor. It uses GStreamer’s installed element registry
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

The executable accepts one JSON object on stdin and writes one JSON object on
stdout only after **Apply**. Canceling or closing the window writes nothing.
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
