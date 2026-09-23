//
// rrclient/gtk.codecpicker.c: codec choser stuff
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
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrclient/ui.speech.h>
#include <rrclient/ui.h>
#include <rrclient/gtk.core.h>
#include <rrclient/cmd.h>
#include <rrclient/media.h>
extern rrconn_t *ws_conn;
extern rrconn_t *ws_tx_conn;   // rrclient/rrclient.c
GtkWidget *tx_combo = NULL;
GtkWidget *rx_combo = NULL;
static bool updating_codecs = false;
static char picker_codec_list[256] = { 0 };
static bool picker_list_initialized = false;
void codec_pickers_refresh(void);
void codec_picker_set_active(bool is_tx, const char *codec);

static int codec_channel_number(const struct rr_client_media_chan *channel) {
   if (!channel) {
      return 0;
   }
   for (int i = 0; i < RR_CLIENT_MEDIA_MAX_CHANS; i++) {
      int number = 0;
      const struct rr_client_media_chan *candidate =
         rrclient_media_chan_iter(i, &number);
      if (candidate == channel) {
         return number;
      }
   }
   return 0;
}

typedef struct {
#if     defined(USE_MONGOOSE)
   struct mg_connection *conn;
#endif // defined(USE_MONGOOSE)
   bool is_tx;
} CodecSelectorCtx;

static void codec_changed_cb(GtkComboBoxText *combo, gpointer user_data) {
   CodecSelectorCtx *ctx = user_data;

   if (!ctx || updating_codecs) {
      return;
   }
   const char *codec = gtk_combo_box_get_active_id( GTK_COMBO_BOX(combo) );
   if (codec) {
      Log( LOG_CRAZY, "gtk.codecpicker", "setting active codec: %s for %s", codec, (ctx->is_tx ? "TX" : "RX") );
      // Tell the server which codec we want for this direction; it spawns
      // the matching fwdsp pipeline and re-announces the channel with the
      // active codec magic.
      // PARITY: rustyrig-www/js/webui.media.js (codec select)
      rrconn_t *cptr = ws_conn;

      if (cptr) {
         const struct rr_client_media_chan *channel =
            rrclient_media_current_channel(ctx->is_tx);
         if (!rrclient_media_select_codec(cptr, ctx->is_tx, codec)) {
            char vfo = channel && channel->vfo < 26 ?
               (char)('A' + channel->vfo) : '-';
            const char *description = channel && channel->descr[0] ?
               channel->descr : "audio";
            ui_print(NULL, "Requested %s codec %s for channel #%d uuid %s VFO %c (%s)",
               ctx->is_tx ? "TX" : "RX", codec,
               codec_channel_number(channel),
               channel ? channel->uuid : "<active>", vfo, description);
         }
      }
   }
}

void populate_codec_combo(GtkComboBoxText *combo, const char *codec_list, const char *default_id) {
   if (!codec_list || !combo) {
      return;
   }
   char *list = g_strdup(codec_list);
   char *saveptr = NULL;
   int index = 1, default_index = 0;

   gtk_combo_box_text_remove_all(combo);
   gtk_combo_box_text_append(combo, "none", "NONE");

   for (char *tok = strtok_r(list, " ", &saveptr) ; tok ; tok = strtok_r(NULL, " ", &saveptr) ) {
      gtk_combo_box_text_append(combo, tok, tok);

      if (default_id && strcmp(tok, default_id) == 0) {
         default_index = index;
      }
      index++;
   }

   if (default_index >= 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(combo), default_index);
   }
   g_free(list);
}

// Re-populate both pickers from the negotiated codec list. Called at GUI
// init (pre-negotiation fallback) and from the media.codecs event once the
// server answers our capab.
// PARITY: rustyrig-www/js/webui.media.js (codec picker population)
void codec_pickers_refresh(void) {
   if (!tx_combo || !rx_combo || updating_codecs) {
      return;
   }
   updating_codecs = true;
   const char *negotiated = media_get_common_codecs();
   const char *default_rx = rrclient_media_current_codec(false);
   const char *default_tx = rrclient_media_current_codec(true);
   char available[sizeof(picker_codec_list)] = { 0 };
   char *configured = NULL;

   if (negotiated) {
      snprintf(available, sizeof(available), "%s", negotiated);
   } else {
      // Not negotiated yet: show our configured preferences; the negotiation
      // (ws_handle_media_msg) will re-select once the server answers.
      configured = (char *)cfg_get_exp("codecs.allowed");

      if (configured) {
         snprintf(available, sizeof(available), "%s", configured);
      }
   }
   free(configured);

   if (!picker_list_initialized || strcmp(available, picker_codec_list) != 0) {
      snprintf(picker_codec_list, sizeof(picker_codec_list), "%s", available);
      picker_list_initialized = true;
      populate_codec_combo(GTK_COMBO_BOX_TEXT(rx_combo), available,
         (default_rx && default_rx[0] ? default_rx : NULL) );
      populate_codec_combo(GTK_COMBO_BOX_TEXT(tx_combo), available,
         (default_tx && default_tx[0] ? default_tx : NULL) );
   } else {
      // The negotiated list is unchanged, so preserve the widgets and only
      // reflect the currently selected channel codec.
      codec_picker_set_active(false, default_rx);
      codec_picker_set_active(true, default_tx);
   }
   updating_codecs = false;
}

// Reflect the codec confirmed by a concrete media subscription without
// rebuilding the other direction's picker.  Subscription notifications are
// authoritative for the channel that was just attached.
void codec_picker_set_active(bool is_tx, const char *codec) {
   GtkWidget *combo = is_tx ? tx_combo : rx_combo;
   if (!combo) {
      return;
   }
   updating_codecs = true;
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo),
      (codec && codec[0]) ? codec : "none");
   updating_codecs = false;
}

// Event: codec negotiation completed (emitted by librrprotocol cli.media.c);
// refresh the pickers from the negotiated list.
static void codec_media_codecs_cb(const char *event, const char *data, rrconn_t *cptr, void *user) {
   codec_pickers_refresh();
}

GtkWidget *create_codec_selector_vbox(GtkWidget **out_tx, GtkWidget **out_rx) {
   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *widget_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(widget_label), "<u>T</u>X/<u>R</u>X Codecs");

   tx_combo = gtk_combo_box_text_new();
   ui_speech_set(tx_combo, "TX Codec",                 // name
      "Transmit Codec",                   // description
      UI_ROLE_COMBOBOX,                     // role
      true);                               // focusable
   gtk_widget_set_tooltip_text(tx_combo, "Transmit codec");
   rx_combo = gtk_combo_box_text_new();
   ui_speech_set(rx_combo, "Receive Codec",            // name
      "Receiver Codec",                   // description
      UI_ROLE_COMBOBOX,                     // role
      true);                               // focusable
   gtk_widget_set_tooltip_text(rx_combo, "Receive codec");
   picker_list_initialized = false;

   CodecSelectorCtx *tx_ctx = g_new0(CodecSelectorCtx, 1);
   CodecSelectorCtx *rx_ctx = g_new0(CodecSelectorCtx, 1);
   tx_ctx->is_tx = true;
   // Populate both pickers; pre-negotiation we show codecs.allowed, and the
   // media.codecs event (below) re-populates once the server answers.
   codec_pickers_refresh();
   event_on("media.codecs", codec_media_codecs_cb, NULL);
#if     defined(USE_MONGOOSE)
   if (ws_tx_conn) {
      tx_ctx->conn = ws_tx_conn->conn;
   }

   if (ws_conn) {
      rx_ctx->conn = ws_conn->conn;
   }
#endif // defined(USE_MONGOOSE)
   rx_ctx->is_tx = false;

   gtk_box_pack_start(GTK_BOX(vbox), widget_label, FALSE, FALSE, 1);
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(tx_combo), TRUE, TRUE, 1);
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(rx_combo), TRUE, TRUE, 1);

   if (out_tx) {
      *out_tx = tx_combo;
   }

   if (out_rx) {
      *out_rx = rx_combo;
   }
   g_signal_connect(tx_combo, "changed", G_CALLBACK(codec_changed_cb), tx_ctx);
   g_signal_connect(rx_combo, "changed", G_CALLBACK(codec_changed_cb), rx_ctx);

   return vbox;
}
