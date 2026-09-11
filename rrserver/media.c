//
// rrserver/media.c: media channel provisioning
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// The server owns the media channel registry. We create one RX and one TX
// audio channel per rig VFO the backend exposes (some devices like the
// Radioberry can RX multiple VFOs independently, so channels are per-VFO,
// never assumed to be a single shared stream). When a client logs in we
// push a `media.available` message per channel; the client subscribes to
// the channels it wants (typically its RX/TX pair) with media.subscribe.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrserver/backend.h>

extern time_t now;

// Create the TX and RX audio channel for a VFO (if not already made)
static void media_setup_vfo(rr_vfo_t vfo) {
   if (vfo < VFO_A || vfo >= MAX_VFOS) {
      return;
   }
   const char *vname = vfo_name(vfo);
   char descr[96];

   snprintf(descr, sizeof(descr), "RX audio VFO %s", vname);
   media_chan_add(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_RX, (uint8_t)vfo, 0, NULL, descr);

   snprintf(descr, sizeof(descr), "TX audio VFO %s", vname);
   media_chan_add(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_TX, (uint8_t)vfo, 0, NULL, descr);
}

// Provision channels for every VFO the rig exposes (rig.vfos in config)
void rrserver_media_init(void) {
   int nvfos = cfg_get_int("rig.vfos", 2);

   if (nvfos < 1) {
      nvfos = 1;
   }
   if (nvfos > MAX_VFOS) {
      nvfos = MAX_VFOS;
   }
   // Prefer the backend's own view of which VFOs exist (e.g. a Radioberry
   // exposes 4 independent RX VFOs); fall back to the rig.vfos config when
   // the backend can't answer yet.
   int made = 0;

   for (int i = 0 ; i < nvfos ; i++) {
      if (rr_be_vfo_supported( (rr_vfo_t)i) ) {
         media_setup_vfo( (rr_vfo_t)i);
         made++;
      }
   }
   if (made == 0) {
      for (int i = 0 ; i < nvfos ; i++) {
         media_setup_vfo( (rr_vfo_t)i);
      }
      made = nvfos;
   }
   Log(LOG_INFO, "ws.media", "Provisioned media channels for %d VFO(s)", made);
}

// Push media.available for every channel to one client. Fired from the
// auth sequence via the send-media-channels event.
static void rrserver_handle_send_media_channels(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   if (!cptr) {
      return;
   }
   media_send_available_all(cptr);
}

void rrserver_media_register_events(void) {
   event_on("send-media-channels", rrserver_handle_send_media_channels, NULL);
}
