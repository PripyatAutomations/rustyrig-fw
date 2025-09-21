//
// rrclient/ws.audio.c: Websocket audio framing / header stuff - WIP
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include <librustyaxe/logger.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/posix.h>
#include <librustyaxe/util.file.h>
#include <rrclient/gtk.core.h>
#include <rrclient/audio.h>
extern time_t now;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern char *negotiated_codecs;		// ws.c

bool ws_audio_init(void) {
   return false;
}

bool ws_select_codec(struct mg_connection *c, const char *codec, bool is_tx) {
   if (!c || !codec) {
      return true;
   }

   const char *jp = dict2json_mkstr(
      VAL_STR, "media.cmd", "codec",
      VAL_STR, "media.codec", codec,
      VAL_STR, "media.channel", (is_tx ? "tx" : "rx"));		// XXX: replace this with UUIDs
   Log(LOG_DEBUG, "ws.audio", "Send %s codec selection: %s", (is_tx ? "TX" : "RX"), jp);
   mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);

   return false;
}
