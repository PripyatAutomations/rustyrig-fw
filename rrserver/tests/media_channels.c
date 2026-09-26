// Verify that server media provisioning creates independent RX/TX channels
// for every backend-supported VFO.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/config.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrserver/backend.h>
#include <rrserver/media.h>

time_t now;
bool dying;
bool restarting;

/* The test supplies a backend with three VFOs. */
bool rr_be_vfo_supported(rr_vfo_t vfo) {
   return vfo >= VFO_A && vfo <= VFO_C;
}

int main(void) {
   cfg = dict_new();
   assert(cfg);
   dict_add_int(cfg, "rig.vfos", 3);
   media_channels_free();

   rrserver_media_init();

   /* The configured three VFOs are provisioned as independent RX/TX pairs. */
   for (uint8_t vfo = 0; vfo < 3; vfo++) {
      struct rr_mediachan *rx = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
         RR_BINFRAME_DIR_RX, vfo, 0);
      struct rr_mediachan *tx = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
         RR_BINFRAME_DIR_TX, vfo, 0);
      assert(rx && tx);
      assert(rx != tx);
      assert(rx->uuid[0] && tx->uuid[0]);
      assert(strstr(rx->descr, "RX audio") != NULL);
      assert(strstr(tx->descr, "TX audio") != NULL);
   }
   assert(!media_chan_find(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_RX, 3, 0));
   assert(!media_chan_find(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_TX, 3, 0));

   media_channels_free();
   dict_free(cfg);
   cfg = NULL;
   puts("PASS: server provisions independent RX/TX media channels per VFO");
   return 0;
}
