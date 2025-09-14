//
// rrclient/ws.media.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "librustyaxe/config.h"
#define	__RRCLIENT	1
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
#include "librustyaxe/logger.h"
#include "librustyaxe/dict.h"
#include "librustyaxe/posix.h"
#include "librustyaxe/util.file.h"
#include "rrclient/auth.h"
#include "mod.ui.gtk3/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "librustyaxe/client-flags.h"

extern dict *cfg;		// config.c
extern time_t now;
extern GtkWidget *tx_codec_combo, *rx_codec_combo;
char *negotiated_codecs = NULL;
char *server_codecs = NULL;

bool ws_handle_media_msg(struct mg_connection *c, dict *d) {
   if (!c || !d) {
      Log(LOG_WARN, "http.ws", "media_msg: got d:<%x> mg_conn:<%x>", d, c);
      return true;
   }

   bool rv = false;

   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   char *media_cmd = dict_get(d, "media.cmd", NULL);
   char *media_payload = dict_get(d, "media.payload", NULL);
   char *media_codecs = dict_get(d, "media.codecs", NULL);

   // Parse the server's capab string and store it's capabilities
   if (media_cmd && strcasecmp(media_cmd, "capab") == 0) {
      Log(LOG_DEBUG, "ws.media", "Got CAPAB from server: %s", media_codecs);

      const char *preferred = dict_get_exp(d, "codecs.allowed");

      if (!media_codecs) {
         Log(LOG_WARN, "ws.media", "Got empty CAPAB from server");
         goto cleanup;
      }

      if (server_codecs) {
         free(server_codecs);
      }
      server_codecs = strdup(media_codecs);

      // Reply with our supported codecs, so the server can negotiate with us
      const char *codec_msg = media_capab_prepare(preferred);

      if (codec_msg) {
         mg_ws_send(c, codec_msg, strlen(codec_msg), WEBSOCKET_OP_TEXT);
         free((void *)codec_msg);
      } else {
         Log(LOG_CRIT, "ws.media", ">> No codecs enabled locally or failed to generate codec message <<");
      }
      free((void *)preferred);
   } else if (media_cmd && strcasecmp(media_cmd, "isupport") == 0) {
      char *media_preferred = dict_get(d, "media.preferred", NULL);

      if (media_codecs) {
         Log(LOG_DEBUG, "ws.media", "Server negotiated codecs: %s, preferred: %s", media_codecs, media_preferred);

         if (negotiated_codecs) {
            free(negotiated_codecs);
         }
         negotiated_codecs = strdup(media_codecs);

         char first_codec[5];
         // Make sure it's all NULLs, so we'll get null terminated string
         memset(first_codec, 0, 5);
         // Copy the *first* codec of the negotiated set, as it's our most preferred.
         memcpy(first_codec, media_codecs, 4);
         populate_codec_combo(GTK_COMBO_BOX_TEXT(tx_codec_combo), media_codecs, (media_preferred ? media_preferred : "pc16"));
         populate_codec_combo(GTK_COMBO_BOX_TEXT(rx_codec_combo), media_codecs, (media_preferred ? media_preferred : "pc16"));
//         ws_select_codec(c, first_codec, false);
      } else {
         Log(LOG_DEBUG, "ws.media", "Got media isupport with empty codecs");
      }
   } else {
      Log(LOG_DEBUG, "ws.media," ">> Unknown media.cmd: %s, .payload: %s", media_cmd, media_payload);
      goto cleanup;
   }
cleanup:
   return false;
}
