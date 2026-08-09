// librrprotocol/au_gst.h
// Simple GStreamer audio abstraction (optional)
#ifndef __librrprotocol_au_gst_h
#define __librrprotocol_au_gst_h

#include <stdbool.h>

#ifdef USE_GSTREAMER
#include <gst/gst.h>
#endif

// Initialize GStreamer resources (no-op if not compiled with USE_GSTREAMER)
extern bool gst_audio_init(void);
// Shutdown GStreamer resources
extern void gst_audio_fini(void);

// Start sending audio on a logical channel (tx); codec is codec id string
extern bool gst_audio_start_tx(uint32_t channel, const char *codec);
extern bool gst_audio_stop_tx(uint32_t channel);

// Start receiving audio for a logical channel (rx)
extern bool gst_audio_start_rx(uint32_t channel, const char *codec);
extern bool gst_audio_stop_rx(uint32_t channel);

#endif // __librrprotocol_au_gst_h
