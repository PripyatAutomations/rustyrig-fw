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
static char webcam_codec[5] = "h264";
static int webcam_fwdsp_chan = -1;

#define WEBCAM_MAX_PROFILES 32
#define WEBCAM_MAX_VIEWERS 128
static struct video_profile {
   char name[32], codec[5], identity[64];
   bool active;
} webcam_profiles[WEBCAM_MAX_PROFILES];
static unsigned webcam_profile_count;
static char webcam_profile_names[512];
static struct video_viewer { rrconn_t *client; int profile; } webcam_viewers[WEBCAM_MAX_VIEWERS];

static int viewer_profile(rrconn_t *client) {
   for (unsigned i = 0; i < WEBCAM_MAX_VIEWERS; i++) if (webcam_viewers[i].client == client) return webcam_viewers[i].profile;
   return -1;
}
static void profile_output(const char *name, const void *data, size_t len, void *user) {
   struct video_profile *profile = user;
   int index = profile - webcam_profiles;
   if (!webcam_chan || len > RR_BINFRAME_MAX_PAYLOAD) return;
   for (rrconn_t *client = http_client_list; client; client = client->next) {
      if (viewer_profile(client) == index && chan_id_in_array(client->rx_channels, MAX_RX_CHANNELS, webcam_chan - media_channels + 1)) ws_media_send_frame(webcam_chan, client, data, len, profile->codec);
   }
}
static void profile_request(const char *event, const void *data, size_t len, rrconn_t *client, void *user) {
   if (len != sizeof(rr_media_profile_request_t) || !webcam_chan) return;
   rr_media_profile_request_t *request = (void *)data;
   if (strcmp(dict_get(request->message, "media.chan-uuid", ""), webcam_chan->uuid)) return;
   request->handled = true;
   const char *name = dict_get(request->message, "media.profile", "");
   int index = -1;
   for (unsigned i = 0; i < webcam_profile_count; i++) if (!strcmp(name, webcam_profiles[i].name)) index = i;
   if (*name && index < 0) { ws_send_error(client, "Unknown configured webcam profile"); return; }
   unsigned slot;
   for (slot = 0; slot < WEBCAM_MAX_VIEWERS; slot++) if (!webcam_viewers[slot].client || webcam_viewers[slot].client == client) break;
   if (slot == WEBCAM_MAX_VIEWERS) { ws_send_error(client, "Webcam viewer limit reached"); return; }
   if (index >= 0 && !webcam_profiles[index].active) {
      struct video_profile *profile = &webcam_profiles[index];
      if (fwdsp_video_pipeline_start(profile->codec, profile->identity, true, profile_output, profile) < 0) {
         ws_send_error(client, "Unable to start configured webcam profile"); return;
      }
      profile->active = true;
   }
   webcam_viewers[slot] = (struct video_viewer){client, index};
   dict *reply = dict_new();
   dict_add(reply, "msg.type", "media");
   dict_add(reply, "media.cmd", "profile");
   dict_add(reply, "media.chan-uuid", webcam_chan->uuid);
   dict_add(reply, "media.profile", name);
   dict_add(reply, "media.codec", index < 0 ? webcam_codec : webcam_profiles[index].codec);
   ws_send_dict(NULL, client, reply, WEBSOCKET_OP_TEXT);
   dict_free(reply);
   request->result = true;
}
static void profile_closed(const char *event, const char *data, rrconn_t *client, void *user) {
   for (unsigned i = 0; i < WEBCAM_MAX_VIEWERS; i++) if (webcam_viewers[i].client == client) webcam_viewers[i].client = NULL;
}
static void capture_subscribed(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *message = data ? json2dict(data) : NULL;
   bool ours = webcam_chan && message && !strcmp(dict_get(message, "media.chan-uuid", ""), webcam_chan->uuid);
   dict_free(message);
   if (!ours) return;
   if (webcam_fwdsp_chan >= 0) {
      struct fwdsp_subproc *capture = fwdsp_find_instance(webcam_codec, false);
      if (capture && capture->process.pid > 0) return;
      webcam_fwdsp_chan = -1;
   }
   webcam_fwdsp_chan = fwdsp_video_start(webcam_codec, false);
   if (webcam_fwdsp_chan < 0) Log(LOG_WARN, "webcam", "Unable to start configured %s.tx capture pipeline", webcam_codec);
   else Log(LOG_INFO, "webcam", "Started configured %s.tx capture pipeline for a viewer", webcam_codec);
}
static void profile_poll(const char *event, const char *data, rrconn_t *client, void *user) {
   if (!webcam_chan) return;
   static uint64_t next_poll;
   uint64_t time = mono_us();
   if (time < next_poll) return;
   next_poll = time + 250000;
   if (webcam_fwdsp_chan >= 0 && !ws_media_channel_has_subscribers(webcam_chan)) {
      fwdsp_codec_stop_immediate(webcam_codec, false);
      webcam_fwdsp_chan = -1;
   }
   unsigned channel = webcam_chan - media_channels + 1;
   bool needed[WEBCAM_MAX_PROFILES] = {0};
   for (unsigned i = 0; i < WEBCAM_MAX_VIEWERS; i++) {
      struct video_viewer *viewer = &webcam_viewers[i];
      if (!viewer->client) continue;
      bool subscribed = chan_id_in_array(viewer->client->rx_channels, MAX_RX_CHANNELS, channel);
      if (!subscribed) { viewer->client = NULL; continue; }
      if (viewer->profile >= 0) needed[viewer->profile] = true;
   }
   for (unsigned i = 0; i < webcam_profile_count; i++) if (webcam_profiles[i].active && !needed[i]) {
      fwdsp_codec_stop_channel_immediate(webcam_profiles[i].codec, true, webcam_profiles[i].identity);
      webcam_profiles[i].active = false;
   }
}
static void profile_catalog(void) {
   dict *configuration = dict_merge_new(default_cfg, cfg);
   int rank = 0;
   const char *key; dict_value_t value; val_type_t type;
   while (configuration && webcam_profile_count < WEBCAM_MAX_PROFILES &&
      (rank = dict_enumerate_typed(configuration, rank, &key, &value, &type)) >= 0) {
      if (!key || strncmp(key, "pipeline:video.", 15)) continue;
      const char *name = key + 15;
      char codec[5]; unsigned fps; int used = 0;
      if (sscanf(name, "%4[a-zA-Z0-9].%u%n", codec, &fps, &used) != 2 || strlen(codec) != 4 || name[used] || !fps || fps > 120 || strlen(name) >= 32) continue;
      const char *pipeline = cfg_get(key);
      if (!pipeline || !*pipeline) continue;
      if (strlen(webcam_profile_names) + strlen(name) + 2 >= sizeof(webcam_profile_names)) continue;
      struct video_profile *profile = &webcam_profiles[webcam_profile_count++];
      snprintf(profile->name, sizeof(profile->name), "%s", name);
      snprintf(profile->codec, sizeof(profile->codec), "%s", codec);
      snprintf(profile->identity, sizeof(profile->identity), "video.%s", name);
      if (webcam_profile_names[0]) strlcat(webcam_profile_names, " ", sizeof(webcam_profile_names));
      strlcat(webcam_profile_names, name, sizeof(webcam_profile_names));
   }
   dict_free(configuration);
}

// Called via event_on_binary() when the fwdsp subprocess emits a captured frame
static void webcam_frame_cb(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user) {
   // The caller hands us the raw frame; the media channel layer owns the wire header
   // (PARITY: librrprotocol/ws.mediachan.c)
   if (webcam_chan && data && len > 0 && len <= RR_BINFRAME_MAX_PAYLOAD) {
      for (rrconn_t *client = http_client_list; client; client = client->next) {
         if (chan_id_in_array(client->rx_channels, MAX_RX_CHANNELS, webcam_chan - media_channels + 1) && viewer_profile(client) < 0) ws_media_send_frame(webcam_chan, client, data, len, webcam_codec);
      }
      for (unsigned i = 0; i < webcam_profile_count; i++) if (webcam_profiles[i].active) {
         if (!fwdsp_write_channel_samples(webcam_profiles[i].codec, true, webcam_profiles[i].identity, data, len)) webcam_profiles[i].active = false;
      }
   }
}

// Provision the video channel; capture starts when a viewer subscribes.
void webcam_init(void) {
   bool enabled = cfg_get_bool("webcam.enable", false);

   if (!enabled) {
      return;
   }

   const char *codec = cfg_get("webcam.codec");

   if (codec && strlen(codec) == 4) {
      snprintf(webcam_codec, sizeof(webcam_codec), "%s", codec);
   }
   // One RX video channel; vfo/rig NA since a webcam isn't tied to a rig, it's station
   // wide
   webcam_chan = media_chan_add(RR_BINFRAME_SUBSYS_VIDEO, RR_BINFRAME_DIR_RX, RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA, webcam_codec, "Webcam video");

   if (!webcam_chan) {
      Log(LOG_CRIT, "webcam", "Failed to provision video media channel");

      return;
   }
   snprintf(webcam_chan->name, sizeof(webcam_chan->name), "station.webcam.rx");
   webcam_profile_names[0] = '\0';
   webcam_chan->profiles = webcam_profile_names;
   profile_catalog();
   event_on_binary("media.profile.request", profile_request, NULL);
   event_on("protocol.session.closed", profile_closed, NULL);
   event_on("protocol.object.close", profile_closed, NULL);
   event_on("server.poll", profile_poll, NULL);
   event_on("media.subscribed", capture_subscribed, NULL);
   // Frames come from the fwdsp subprocess via the event bus. The pipeline
   // itself is defined in pipeline config; GStreamer
   // lives entirely inside the subprocess, never linked into rrserver.
   char event_name[32];
   memset(event_name, 0, sizeof(event_name));
   snprintf(event_name, sizeof(event_name), "fwdsp.frame.%.4s", webcam_codec);
   event_on_binary(event_name, webcam_frame_cb, NULL);

}

// Stop the capture subprocess; used on shutdown
void webcam_shutdown(void) {
   event_off("server.poll", profile_poll, NULL);
   event_off("media.subscribed", capture_subscribed, NULL);
   char event_name[32];
   snprintf(event_name, sizeof(event_name), "fwdsp.frame.%.4s", webcam_codec);
   event_off_binary(event_name, webcam_frame_cb, NULL);
   event_off("protocol.session.closed", profile_closed, NULL);
   event_off("protocol.object.close", profile_closed, NULL);
   event_off_binary("media.profile.request", profile_request, NULL);
   for (unsigned i = 0; i < webcam_profile_count; i++) if (webcam_profiles[i].active)
      fwdsp_codec_stop_channel_immediate(webcam_profiles[i].codec, true, webcam_profiles[i].identity);
   webcam_profile_count = 0;
   memset(webcam_viewers, 0, sizeof(webcam_viewers));
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
