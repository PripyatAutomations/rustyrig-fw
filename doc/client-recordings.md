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
name prompt appears; Cancel leaves the automatically generated name. An
example filename is:

`RX - 14370000USB - rplywv00-rig0.rplywv00-rig0.A - 2026-11-10_00-00-00 - 2026-11-10_00-12-34.ogg`

Recordings include available title, user, timestamp, frequency, mode, site,
rig and VFO tags. The core-thread snapshot supplies the logged-in username, connected server,
room/rig/VFO, grid and all currently routed RX stream identifiers; GTK does not
read live connection structures. When a Maidenhead grid is available, latitude/longitude tags contain its
**approximate center**, explicitly marked with LOCATION_SOURCE; these are not
claimed to be actual GPS measurements. Filenames use readable start/end
local timestamps, without UUIDs. The unique recording ID remains in the
Ogg/FLAC metadata, linking formats from the same recording. If a filename
already exists, a numeric suffix such as `(2)` is added before recording.

The list can play files through the existing RX PCM engine by decoding into
two separately routed L/R input streams. The Play and Stop Playback buttons
control this source. Rename/Move offers a new name and a directory chooser
showing available folders, with moves restricted to the managed recording tree.
Paired Ogg/FLAC formats move together. Download copies to a selected
destination, New Folder makes subdirectories, and Delete
confirms before removing an item. Directories must be empty to delete.

## Dependencies and design notes

`ffmpeg` must be installed and have the Vorbis and FLAC encoders and
decoders enabled. This is declared as a dependency of rustyrig-client-gtk
in Debian packages.

Encoding and decoding run in child processes on worker threads. The audio
callback uses a bounded, four-second ring buffer and a nonblocking mutex
attempt, preventing slow disks or encoders from blocking the PCM hub. A
failed/full queue can drop samples; diagnostics and more detailed recording
status still need integration.

This is a new **client** recording facility, separate from server PTT and RX
FLAC recording documented in `doc/recordings.md`.

## Waveform playback (GTK)

Double-click an Ogg/FLAC file or select **Play** in the recordings manager to
open an independently movable **Recording Playback** floating panel. The
waveform is rendered as separate left and right peak-envelope tracks. Analysis
is performed asynchronously with ffmpeg at 800 stereo frames per second and
bounded to 4096 peak columns; it never decodes an entire file on the GTK
thread or holds the entire file in memory.

**Click anywhere in the waveform** to start playing from that position; the
vertical playhead follows playback progress. Controls include Play, Pause, Stop,
Reverse and Delete. Reverse plays audio backwards using short four-second
decode windows rather than buffering the entire recording. Selecting Reverse
before Play starts from the end. A file deletion requires confirmation and
updates the recording browser. Only files in the local managed recordings
directory can be opened/deleted via the player.

Playback requests are serialized into the core event domain and feed two
individually routed L/R PCM hub streams. GTK owns drawing and panels; PCM
and ffmpeg workers never call GTK. Hovering, focusing, or opening a waterfall
source/VFO selector makes it fully opaque. While idle, these drop to
36% opacity so they don't obscure the displayed spectrum.
