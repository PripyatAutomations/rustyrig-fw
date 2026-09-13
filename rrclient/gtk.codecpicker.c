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
#include <rrclient/gtk.core.h>
extern rrconn_t *ws_conn;
extern rrconn_t *ws_tx_conn;   // rrclient/rrclient.c
GtkWidget *tx_combo = NULL;
GtkWidget *rx_combo = NULL;

typedef struct {
#if     defined(USE_MONGOOSE)
   struct mg_connection *conn;
#endif // defined(USE_MONGOOSE)
   bool is_tx;
} CodecSelectorCtx;

static void codec_changed_cb(GtkComboBoxText *combo, gpointer user_data) {
   CodecSelectorCtx *ctx = user_data;

   if (!ctx) {
      return;
   }
   const char *codec = gtk_combo_box_get_active_id( GTK_COMBO_BOX(combo) );
   if (codec) {
      Log( LOG_CRAZY, "gtk.codecpicker", "setting active codec: %s for %s", codec, (ctx->is_tx ? "TX" : "RX") );
      // Tell the server which codec we want for this direction; it spawns
      // the matching fwdsp pipeline and re-announces the channel with the
      // active codec magic.
      // PARITY: rustyrig-www/js/webui.media.js (codec select)
      rrconn_t *cptr = (ctx->is_tx ? (ws_tx_conn ? ws_tx_conn : ws_conn) : ws_conn);

      if (cptr) {
         media_send_codec_select(cptr, codec, (ctx->is_tx ? "tx" : "rx") );
      }
   }
}

void populate_codec_combo(GtkComboBoxText *combo, const char *codec_list, const char *default_id) {
   if (!codec_list || !combo || !default_id) {
      return;
   }
   char *list = g_strdup(codec_list);
   char *saveptr = NULL;
   int index = 0, default_index = -1;

   gtk_combo_box_text_remove_all(combo);

   for (char *tok = strtok_r(list, " ", &saveptr) ; tok ; tok = strtok_r(NULL, " ", &saveptr) ) {
      Log(LOG_CRAZY, "gtk.codecpicker", "Adding codec |%s| to list <%x>", tok, combo);
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

   CodecSelectorCtx *tx_ctx = g_new0(CodecSelectorCtx, 1);
   CodecSelectorCtx *rx_ctx = g_new0(CodecSelectorCtx, 1);
   tx_ctx->is_tx = true;
   // Populate both pickers from our negotiated common codecs (falls back to
   // codecs.allowed before negotiation completes), defaulting to whichever
   // codec the negotiation picked for that direction.
   // PARITY: rustyrig-www/js/webui.media.js (codec picker population)
   const char *negotiated = media_get_common_codecs();
   const char *default_rx = media_get_codec(false);
   const char *default_tx = media_get_codec(true);

   if (negotiated) {
      populate_codec_combo(GTK_COMBO_BOX_TEXT(rx_combo), negotiated,
         (default_rx && default_rx[0] ? default_rx : "mu08") );
      populate_codec_combo(GTK_COMBO_BOX_TEXT(tx_combo), negotiated,
         (default_tx && default_tx[0] ? default_tx : "mu08") );
   } else {
      // Not negotiated yet: show our configured preferences; the negotiation
      // (ws_handle_media_msg) will re-select once the server answers.
      const char *my_codecs = cfg_get_exp("codecs.allowed");

      if (my_codecs) {
         populate_codec_combo(GTK_COMBO_BOX_TEXT(rx_combo), my_codecs, NULL);
         populate_codec_combo(GTK_COMBO_BOX_TEXT(tx_combo), my_codecs, NULL);
         free( (void *)my_codecs );
      }
   }
#if     defined(USE_MONGOOSE)
   if (ws_tx_conn) {
      tx_ctx->conn = ws_tx_conn->conn;
   }

   if (ws_conn) {
      rx_ctx->conn = ws_conn->conn;
   }
#endif // defined(USE_MONGOOSE)
   rx_ctx->is_tx = false;

   gtk_combo_box_set_active(GTK_COMBO_BOX(tx_combo), 1);
   gtk_combo_box_set_active(GTK_COMBO_BOX(rx_combo), 1);

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
