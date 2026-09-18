//
// librrprotocol/cli.media.c: client-side media/codec negotiation
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Implements the media.* text-frame negotiation described in
// doc/media-frames.md. Server side is librrprotocol/srv.http.c.
//
// PARITY: rustyrig-www/js/webui.js (media message handling)
//
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/codecneg.h>
#include <librrprotocol/ws.h>
#include <librrprotocol/ws.mediachan.h>

// PARITY: librrprotocol/ws.mediachan.c (server side of the same messages)

// Negotiation state for the connection; single-server client for now
static char cli_common_codecs[256] = { 0 };
static char cli_preferred_codec[5] = { 0 };
static char cli_codec_tx[5] = { 0 };
static char cli_codec_rx[5] = { 0 };

extern time_t now;

const char *media_get_common_codecs(void) {
   return cli_common_codecs[0] ? cli_common_codecs : NULL;
}

const char *media_get_preferred_codec(void) {
   return cli_preferred_codec[0] ? cli_preferred_codec : NULL;
}

const char *media_get_codec(bool is_tx) {
   return (is_tx ? cli_codec_tx : cli_codec_rx);
}

// Client -> server: select a codec for one concrete media channel.
// The UUID is authoritative; direction comes from the server-owned channel.
bool media_send_codec_select(rrconn_t *cptr, const char *codec, const char *channel_uuid) {
   if (!cptr || !codec || strlen(codec) != 4 || !channel_uuid || !*channel_uuid) {
      Log(LOG_WARN, "ws.media", "media.codec select: invalid args codec:<%p> uuid:<%p>",
         codec, channel_uuid);
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "codec");
   dict_add(d, "media.codec", codec);
   dict_add(d, "media.chan-uuid", channel_uuid);
   dict_add_ulong(d, "media.ts", now);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   Log(LOG_INFO, "ws.media", "Selected codec %s for media channel %s", codec, channel_uuid);
   return false;
}

// Client -> server: send our own capability list
bool media_send_client_capab(rrconn_t *cptr) {
   if (!cptr) {
      return true;
   }
   const char *my_codecs = cfg_get_exp("codecs.allowed");

   if (!my_codecs || !*my_codecs) {
      free( (void *)my_codecs );
      Log(LOG_WARN, "ws.media", "codecs.allowed not set; cannot send client capab");
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "capab");
   dict_add(d, "media.codecs", my_codecs);
   dict_add_ulong(d, "media.ts", now);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   free( (void *)my_codecs );
   Log(LOG_DEBUG, "ws.media", "Sent client media.capab");

   return false;
}

// Handle a media.* text frame from the server. Registered in the
// ws_routes_cli route table.
bool ws_handle_media_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      return true;
   }
   const char *media_cmd = dict_get(d, "media.cmd", NULL);

   if (!media_cmd) {
      Log(LOG_DEBUG, "ws.media", "media message without media.cmd");
      return true;
   }

   if (strcasecmp(media_cmd, "capab") == 0) {
      // Server is telling us its supported codecs; intersect with ours
      // and pick a default, then select codecs for both directions.
      const char *media_codecs = dict_get(d, "media.codecs", NULL);

      if (!media_codecs || !*media_codecs) {
         Log(LOG_WARN, "ws.media", "media.capab without codecs list");
         return true;
      }
      const char *my_codecs = cfg_get_exp("codecs.allowed");

      if (!my_codecs) {
         Log(LOG_CRIT, "ws.media", "codecs.allowed must be set to negotiate codecs!");
         return true;
      }
      char *common = codec_filter_common(my_codecs, media_codecs);
      free( (void *)my_codecs );

      if (!common || strlen(common) < 4) {
         Log(LOG_CRIT, "ws.media", "No codecs in common with server! (mine: |%s|, server: |%s|)",
            (common ? common : "<none>"), media_codecs);
         free(common);

         return true;
      }
      // The first common codec is our default/preferred
      memset(cli_preferred_codec, 0, sizeof(cli_preferred_codec));
      memcpy(cli_preferred_codec, common, 4);
      snprintf(cli_common_codecs, sizeof(cli_common_codecs), "%s", common);
      Log(LOG_INFO, "ws.media", "Negotiated common codecs: %s (default: %s)", common, cli_preferred_codec);

      // Channel selection is UUID-specific and is performed by rrclient/media.c
      // when each media.available announcement arrives.

      // Tell the program (UI) negotiation completed so codec pickers can
      // re-populate with the negotiated list. The dict already carries
      // media.codecs + our selected codec; programs listen with event_on().
      event_emit_dict("media.codecs", cptr, d);

      free(common);

      return false;
   } else if (strcasecmp(media_cmd, "isupport") == 0) {
      const char *media_codecs = dict_get(d, "media.codecs", NULL);
      const char *media_preferred = dict_get(d, "media.preferred", NULL);
      uint32_t dir = dict_get_ulong(d, "media.dir", RR_BINFRAME_DIR_NA);

      if (media_preferred && strlen(media_preferred) == 4) {
         char *dst = NULL;

         if (dir == RR_BINFRAME_DIR_TX) {
            dst = cli_codec_tx;
         } else if (dir == RR_BINFRAME_DIR_RX) {
            dst = cli_codec_rx;
         }
         if (dst) {
            memcpy(dst, media_preferred, 4);
            dst[4] = '\0';
         }
      }
      Log(LOG_INFO, "ws.media", "Server confirms codecs: %s (preferred: %s)",
         (media_codecs ? media_codecs : "<none>"), (media_preferred ? media_preferred : "<none>"));

      return false;
   } else if (strcasecmp(media_cmd, "available") == 0 ||
              strcasecmp(media_cmd, "subscribed") == 0 ||
              strcasecmp(media_cmd, "unsubscribed") == 0 ||
              strcasecmp(media_cmd, "chan-remove") == 0) {
      // Channel subscription notifications: handled by the program via the
      // ws.msg.media event (PARITY: rrclient/events.c rrclient_handle_media);
      // nothing to do at the wire level here, so don't log it as unhandled.
      return false;
   }
   Log(LOG_DEBUG, "ws.media", "Unhandled media cmd: |%s|", media_cmd);

   return true;
}

// Client -> server: ask for the current media channel list
// PARITY: librrprotocol/ws.mediachan.c (list handling)
bool media_send_list(rrconn_t *cptr) {
   if (!cptr) {
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "list");
   dict_add_ulong(d, "media.ts", now);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

// Client -> server: subscribe to a media channel by uuid
// PARITY: rustyrig-www/js/webui.media.js (subscribeMediaChannel)
bool media_send_subscribe(rrconn_t *cptr, const char *uuid) {
   if (!cptr || !uuid || uuid[0] == '\0') {
      Log(LOG_WARN, "ws.media", "media_send_subscribe: invalid args");
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "subscribe");
   dict_add(d, "media.chan-uuid", uuid);
   dict_add_ulong(d, "media.ts", now);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   Log(LOG_INFO, "ws.media", "Subscribing to media channel |%s|", uuid);

   return false;
}

// Client -> server: unsubscribe from a media channel by uuid
bool media_send_unsubscribe(rrconn_t *cptr, const char *uuid) {
   if (!cptr || !uuid || uuid[0] == '\0') {
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "unsubscribe");
   dict_add(d, "media.chan-uuid", uuid);
   dict_add_ulong(d, "media.ts", now);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

// Client -> server: register this connection as a media source. Requires
// the account to have the media.source priv. With uuid == NULL the source
// registers for all channels; with a uuid it registers for that channel.
// PARITY: librrprotocol/ws.mediachan.c (source handling)
bool media_send_source(rrconn_t *cptr, const char *uuid) {
   if (!cptr) {
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "source");
   dict_add_ulong(d, "media.ts", now);

   if (uuid && uuid[0] != '\0') {
      dict_add(d, "media.chan-uuid", uuid);
   }
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   Log(LOG_INFO, "ws.media", "Registering as media source (channel %s)",
      (uuid && uuid[0] ? uuid : "<all>"));

   return false;
}
