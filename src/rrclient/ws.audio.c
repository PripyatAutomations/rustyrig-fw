//
// gtk-client/ws.audio.c: Websocket audio framing / header stuff - WIP
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
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
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
extern time_t now;
extern time_t poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN+1];
extern const char *get_chat_ts(void);
extern GtkWidget *main_window;
extern struct mg_mgr mgr;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern bool server_ptt_state;
extern char *negotiated_codecs;		// ws.c

bool ws_audio_init(void) {
   return false;
}

// if passed NULL for codecs, use all available codecs
bool ws_send_capab(struct mg_connection *c, const char *codecs) {
   if (!codecs) {
      char *capab_msg = codecneg_send_supported_codecs(au_core_codecs);

      if (!capab_msg) {
         return true;
      }
      mg_ws_send(c, capab_msg, strlen(capab_msg), WEBSOCKET_OP_TEXT);
      Log(LOG_DEBUG, "codecneg", "Sending capab msg: %s", capab_msg);
      free(capab_msg);
   } else {
      // Handle common codecs check
      const char *preferred = cfg_get("codecs.allowed");
      char *common_codecs = codec_filter_common(preferred, codecs);

      if (!common_codecs) {
         return true;
      }

      if (common_codecs && strlen(common_codecs) >= 4) {		// valid IDs are 4 char long
         // emit codec message
         char msgbuf[1024];
         snprintf(msgbuf, sizeof(msgbuf), "{ \"media\": { \"cmd\": \"capab\", \"payload\": \"%s\" } }", common_codecs);
         Log(LOG_DEBUG, "ws.audio", "Sending codec request: %s", msgbuf);
      }
      free(common_codecs);
   }     
   return false;
}

bool ws_select_codec(struct mg_connection *c, const char *codec) {
   if (!codec) {
      return true;
   }

   char msgbuf[1024];
   memset(msgbuf, 0, sizeof(msgbuf));
   snprintf(msgbuf, sizeof(msgbuf), "{ \"media\": { \"cmd\": \"codec\", \"codec\": \"%s\", \"channel\": \"rx\" } }", codec);
   Log(LOG_DEBUG, "ws.audio", "Send RX codec selection: %s", msgbuf);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   // and for TX
   memset(msgbuf, 0, sizeof(msgbuf));
   snprintf(msgbuf, sizeof(msgbuf), "{ \"media\": { \"cmd\": \"codec\", \"codec\": \"%s\", \"channel\": \"tx\" } }", codec);
   Log(LOG_DEBUG, "ws.audio", "Send TX codec selection: %s", msgbuf);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   return false;
}
