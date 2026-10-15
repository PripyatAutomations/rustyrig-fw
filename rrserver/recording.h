// Server recording policy; transport codec and RF mode are separate choices.
#ifndef RRSERVER_RECORDING_H
#define RRSERVER_RECORDING_H
#include <strings.h>
#include <librustyaxe/core.h>
#include <librrprotocol/vfo.h>
static inline bool rrserver_recording_enabled(bool tx) {
   const char *scoped = tx ? "fwdsp:recording.tx" : "fwdsp:recording.rx";
   const char *configured = cfg_get(scoped);
   if (configured && *configured) return cfg_get_bool(scoped, false);
   return cfg_get_bool(tx ? "record.tx" : "record.rx", tx);
}
static inline const char *rrserver_recording_codec(rr_mode_t mode) {
   if (mode == MODE_DU || mode == MODE_DL || mode == MODE_FT4 || mode == MODE_FT8) return "flac";
   const char *codec = cfg_get("fwdsp:recording.codec");
   if (!codec || !*codec) codec = cfg_get("recording.codec");
   if (!codec || !*codec) return "ogg";
   return !strcasecmp(codec, "ogg") ? "ogg" : "flac";
}
#endif
