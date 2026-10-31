# librrsdrutils — experimental skeleton

Portable SDR signal primitives for the RustyRig Cooperative Resource Graphs design.

This directory is deliberately **not built or linked yet**. The draft types below
are for local C DSP planning and validation, **not a stable ABI and not a mesh
wire protocol**. See [architecture](../doc/cooperative-resource-graphs.md).

Planned modules: IQ format conversion, NCO, FIR, decimation, resampling,
channelizers, FFT and testing with synthetic IQ. Neither this library nor
the eventual graph executor should depend on GStreamer, GTK, hardware SDKs,
or mesh transport.

`rrsdr` will eventually provide capture backends and execute slice graphs.
The existing `rrbridge` remains an adapter for externally managed receivers.
