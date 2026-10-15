#include <assert.h>
#include <stdio.h>
#include <rrserver/recording.h>
extern defconfig_t defcfg[];
time_t now;
bool dying, restarting;
int main(void) {
   default_cfg = dict_new();
   assert(cfg_set_defaults(default_cfg, defcfg));
   cfg = default_cfg;
   assert(!rrserver_recording_enabled(false));
   assert(rrserver_recording_enabled(true));
   assert(!strcmp(rrserver_recording_codec(MODE_USB), "ogg"));
   assert(!strcmp(rrserver_recording_codec(MODE_DMR), "ogg"));
   const rr_mode_t data[] = {
      MODE_DU, MODE_DL, MODE_FT4, MODE_FT8
   };

   for (unsigned i = 0 ; i < sizeof(data) / sizeof(data[0]) ; i++) {
      assert(!strcmp(rrserver_recording_codec(data[i]), "flac"));
   }

   dict_add(cfg, "fwdsp:recording.tx", "false");
   assert(!rrserver_recording_enabled(true));
   dict_add(cfg, "fwdsp:recording.rx", "true");
   assert(rrserver_recording_enabled(false));
   dict_add(cfg, "fwdsp:recording.codec", "flac");
   assert(!strcmp(rrserver_recording_codec(MODE_USB), "flac"));
   dict_add(cfg, "fwdsp:recording.codec", "ogg");
   assert(!strcmp(rrserver_recording_codec(MODE_FT8), "flac"));
   dict_free(cfg);
   cfg = default_cfg = NULL;
   puts("PASS: server defaults RX off/TX on/Ogg voice, explicit disable and FLAC data modes");
}
