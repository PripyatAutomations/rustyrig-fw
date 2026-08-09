// librrprotocol/au_gst.c
// Minimal GStreamer integration scaffolding. When compiled with -DUSE_GSTREAMER
// this will attempt to initialize GStreamer. For now these functions are
// lightweight wrappers and logging hooks for future pipeline integration.

#include <librustyaxe/core.h>
#include <librrprotocol/au_gst.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_GSTREAMER
#include <gst/gst.h>
static bool gst_initialized = false;
#endif

bool gst_audio_init(void) {
#ifdef USE_GSTREAMER
   if (gst_initialized) {
      return true;
   }
   int argc = 0;
   char **argv = NULL;
   if ( !gst_init_check(&argc, &argv, NULL) ) {
      Log(LOG_CRIT, "au_gst", "GStreamer failed to initialize");

      return true;
   }
   gst_initialized = true;
   Log(LOG_DEBUG, "au_gst", "GStreamer initialized");

   return false;
#else
   Log(LOG_DEBUG, "au_gst", "GStreamer not enabled in build");

   return true;   // indicate not initialized when not compiled in
#endif
}

void gst_audio_fini(void) {
#ifdef USE_GSTREAMER
   if (!gst_initialized) {
      return;
   }
   gst_deinit();
   gst_initialized = false;
   Log(LOG_DEBUG, "au_gst", "GStreamer deinitialized");
#else
   (void)0;
#endif
}

bool gst_audio_start_tx(uint32_t channel, const char *codec) {
   Log(LOG_DEBUG, "au_gst", "Start TX channel %u codec %s", channel, codec ? codec : "(null)");

   // TODO: create a pipeline for capture -> encode -> network
   return false;
}

bool gst_audio_stop_tx(uint32_t channel) {
   Log(LOG_DEBUG, "au_gst", "Stop TX channel %u", channel);

   // TODO: stop and free the pipeline
   return false;
}

bool gst_audio_start_rx(uint32_t channel, const char *codec) {
   Log(LOG_DEBUG, "au_gst", "Start RX channel %u codec %s", channel, codec ? codec : "(null)");

   // TODO: create a pipeline for network -> decode -> playback
   return false;
}

bool gst_audio_stop_rx(uint32_t channel) {
   Log(LOG_DEBUG, "au_gst", "Stop RX channel %u", channel);

   // TODO: stop and free the pipeline
   return false;
}
