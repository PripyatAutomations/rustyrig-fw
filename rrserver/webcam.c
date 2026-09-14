// rrserver/webcam.c: Support for a webcam via gstreamer, usually pointed at rig
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

// When webcam.enable is set in the config, we spawn a fwdsp subprocess (which
// runs the gstreamer v4l2 capture pipeline; gstreamer never links into us) and
// fan the encoded frames out to every subscriber of the SUBSYS_VIDEO media
// channel. The subprocess hands us frames via the fwdsp.frame.<codec> event.
#include <libfwdspmgr/fwdsp-mgr.h>

static struct rr_mediachan *webcam_chan = NULL;
static const char *webcam_codec = "jpeg";
static int webcam_fwdsp_chan = -1;

// Called via event_on_binary() when the fwdsp subprocess emits a captured frame
static void webcam_frame_cb(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user) {
   (void)event; (void)cptr; (void)user;
   // The caller hands us the raw frame; the media channel layer owns the wire
   // header (PARITY: librrprotocol/ws.mediachan.c)
   if (webcam_chan && data && len > 0) {
      ws_media_broadcast_subscribed(webcam_chan, (const uint8_t *)data, len, webcam_codec);
   }
}

// Provision the video media channel and spawn the fwdsp capture subprocess
void webcam_init(void) {
   bool enabled = cfg_get_bool("webcam.enable", false);

   if (!enabled) {
      return;
   }
   const char *device = cfg_get("webcam.device");

   if (!device || device[0] == '\0') {
      device = "/dev/video0";
   }
   const char *codec = cfg_get("webcam.codec");

   if (codec && strlen(codec) == 4) {
      webcam_codec = codec;
   }
   // One RX video channel; vfo/rig NA since a webcam isn't tied to a rig
   webcam_chan = media_chan_add(RR_BINFRAME_SUBSYS_VIDEO, RR_BINFRAME_DIR_RX,
      RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA, webcam_codec, "Webcam video");

   if (!webcam_chan) {
      Log(LOG_CRIT, "webcam", "Failed to provision video media channel");
      return;
   }
   // Frames come from the fwdsp subprocess via the event bus. The pipeline
   // itself (v4l2src -> jpegenc) is defined in the fwdsp config; gstreamer
   // lives entirely inside the subprocess, never linked into rrserver.
   char event_name[32];

   snprintf(event_name, sizeof(event_name), "fwdsp.frame.%.4s", webcam_codec);
   event_on_binary(event_name, webcam_frame_cb, NULL);
   webcam_fwdsp_chan = fwdsp_video_start(webcam_codec, false);

   if (webcam_fwdsp_chan < 0) {
      Log(LOG_CRIT, "webcam", "Failed to start fwdsp video pipeline for %s (device %s)",
         webcam_codec, device);
   } else {
      Log(LOG_INFO, "webcam", "Webcam capture started via fwdsp on %s (codec %s, channel %s)",
         device, webcam_codec, webcam_chan->uuid);
   }
}

// Stop the capture subprocess; used on shutdown
void webcam_shutdown(void) {
   if (webcam_fwdsp_chan >= 0) {
      fwdsp_codec_stop(webcam_codec, false);
      webcam_fwdsp_chan = -1;
   }
   if (webcam_chan) {
      media_send_chan_removed_all(webcam_chan);
      media_chan_remove(webcam_chan->uuid);
      webcam_chan = NULL;
   }
}
