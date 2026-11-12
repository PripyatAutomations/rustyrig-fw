# Client stereo recordings

The GTK client has a **Recordings** toolbar tool. Its floating panel uses the
shared GTK tree-row striping and lists local Ogg and FLAC files and folders
under `~/.local/share/rustyrig/recordings`.

Click **RECORD** to capture the output of the client's stereo RX mixer. The
format chooser supports Ogg/Vorbis (default), FLAC and Both. The Both option
writes two copies of the same PCM in parallel with a shared
`RECORDING_ID` Vorbis/FLAC tag. The output is 16 kHz two-channel audio, as
presented to the client playback DSP before the final speaker sink.

Click **STOP** to close the encoder input. When encoding has finished, a
name prompt appears; Cancel leaves the automatically generated name. Example filenames now use a readable frequency and timestamps, without forcing
unknown CAT modes into the name:

`2026-11-12_15-38-00 - 10.004 MHz - Local_monitor - RX - end-15-38-07.ogg`

`2026-11-12_15-38-00 - 14.37 MHz USB - rplywv00.rig0.A - RX - end-15-38-07.flac`

The start date is included only once; the ending clock time follows `end-`.
The full, unrounded frequency, CAT mode, rig and receiver identities remain
available in the codec metadata. New filenames are generated for new recordings;
existing files are not renamed automatically.

Recordings include available title, user, timestamp, frequency, mode, site,
rig and VFO tags. The core-thread snapshot supplies the logged-in username, connected server,
room/rig/VFO, grid and all currently routed RX stream identifiers; GTK does not
read live connection structures. When a Maidenhead grid is available, latitude/longitude tags contain its
**approximate center**, explicitly marked with LOCATION_SOURCE; these are not
claimed to be actual GPS measurements. Filenames use readable start/end
local timestamps, without UUIDs. The unique recording ID remains in the
Ogg/FLAC metadata, linking formats from the same recording. If a filename
already exists, a numeric suffix such as `(2)` is added before recording.

The recording manager presents a hierarchical, expandable folder/file tree (up to
24 directory levels and 3000 entries for responsiveness). A `..` parent entry
appears while browsing a subfolder, but never at the Recordings root.
Drag a recording onto a folder to move it, including its Ogg/FLAC companion
when present; invalid moves outside the recordings root and collisions
are rejected. Symlinked folders are not traversed. The Rename / Move dialog
remains available for renaming or choosing a destination explicitly.

The list can play files through the existing RX PCM engine by decoding into
two separately routed L/R input streams. The Play and Stop Playback buttons
control this source. At natural EOF the worker now marks its L/R streams as
finite, allowing the final sub-80 ms PCM block to bypass the normal
startup prebuffer; the playback-finished event occurs only after the mixer
queues drain. An explicit Stop still terminates immediately. Rename/Move offers a new name and a dropdown listing
the Recordings root and all existing nested subdirectories. Moves are
restricted to the managed recording tree.
Paired Ogg/FLAC formats move together. Download copies to a selected
destination, New Folder makes subdirectories, and Delete
confirms before removing an item. Directories must be empty to delete.

## Dependencies and design notes

Recording, playback, and waveform analysis use native `libFLAC`,
`libvorbis`, `libvorbisfile` and `libogg` APIs. FFmpeg is not needed for
client recordings. Development headers are required at build time; normal
client packages link the corresponding runtime libraries.

Encoding and decoding run in-process on isolated worker threads. The audio
callback uses a bounded, four-second ring buffer and a nonblocking mutex
attempt, preventing slow disks or codec processing from blocking the PCM hub. A
failed/full queue can drop samples; diagnostics and more detailed recording
status still need integration.

This is a new **client** recording facility, separate from server PTT and RX
FLAC recording documented in `doc/recordings.md`.

## Waveform playback (GTK)

Double-click an Ogg/FLAC file or select **Play** in the recordings manager to
open an independently movable **Recording Playback** floating panel. The
waveform is rendered as separate left and right peak-envelope tracks. Analysis
is performed asynchronously through native decoders and normalized to 16 kHz,
then accumulated into 800 Hz peak-envelope groups and bounded to 4096 peak
columns. It never decodes on the GTK thread or holds the entire file in memory.

**Click anywhere in the waveform** to start playing from that position; the
vertical playhead follows playback progress. Controls include Play, Pause, Stop,
Reverse and Delete. Reverse plays audio backwards using short four-second
decode windows rather than buffering the entire recording. Selecting Reverse
before Play starts from the end. A file deletion requires confirmation and
updates the recording browser. Only files in the local managed recordings
directory can be opened/deleted via the player.

Playback requests are serialized into the core event domain and feed two
individually routed L/R PCM hub streams. GTK owns drawing and panels; PCM
and recording codec workers never call GTK. Hovering, focusing, or opening a waterfall
source/VFO selector makes it fully opaque. While idle, these drop to
36% opacity so they don't obscure the displayed spectrum.

## Audio Graph — permanent recording playback source

The client Audio Graph always contains a **Recording Playback** stereo
source (`recording.playback`), even while no file is playing. Its two
underlying PCM hub streams are retained and routed independently to the left
and right speakers. Drag/click its L/R wires to select the playback
destinations; those routes are remembered in the client settings database.
Play, stop and natural end-of-file do not remove the node or erase its
routing. Changing the destination during playback preserves buffered PCM.

Each graph input port (including Recording Playback) has a small gain fader
immediately beside its connection, with a **0 dB (unity) default**, range
-60 to +12 dB, and independent left/right settings. Adjustments execute in
the core-owned PCM mixer and persist per source/port, independent of the
global RX volume. The per-source input trim does not alter the original
decoded PCM fed to the standalone RX waterfall or local modem decoders.
The routed stereo mix/monitor recording captures the *pre-input-trim,
pre-master-gain* signal. Changes to any listening gain slider never
change the recorded samples.
