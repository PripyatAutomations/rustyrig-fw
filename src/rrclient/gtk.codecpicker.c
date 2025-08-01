// rrclient/gtk.codecpicker.c: codec choser stuff
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
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern bool parse_chat_input(GtkButton *button, gpointer entry);	// chat.cmd.c
extern bool clear_syslog(void);
extern GtkWidget *init_log_tab(void);
extern dict *cfg;
extern time_t now;
extern bool dying;
extern bool ptt_active;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern GtkWidget *userlist_init(void);
extern time_t poll_block_expire, poll_block_delay;
extern GstElement *rx_vol_gst_elem;		// audio.c
extern GstElement *rx_pipeline;			// audio.c
extern GtkWidget *config_tab;

GtkComboBoxText *tx_combo = NULL;
GtkComboBoxText *rx_combo = NULL;

typedef struct {
   struct mg_connection *conn;
   bool is_tx;
} CodecSelectorCtx;

static void codec_changed_cb(GtkComboBoxText *combo, gpointer user_data) {
   CodecSelectorCtx *ctx = user_data;
   const char *codec = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));

   if (codec) {
      Log(LOG_DEBUG, "gtk.codec", "setting active codec: %s for %s", codec, (ctx->is_tx ? "TX" : "RX"));
      // we need to invert is_tx since we're asking the server to set itself up to match our needs
      ws_select_codec(ctx->conn, codec, !ctx->is_tx);
   }
}

void populate_codec_combo(GtkComboBoxText *combo, const char *codec_list, const char *default_id) {
   char *list = g_strdup(codec_list);
   char *saveptr = NULL;
   int index = 0, default_index = -1;

   gtk_combo_box_text_remove_all(combo);

   for (char *tok = strtok_r(list, " ", &saveptr); tok; tok = strtok_r(NULL, " ", &saveptr)) {
      gtk_combo_box_text_append(combo, tok, tok);

      if (default_id && strcmp(tok, default_id) == 0)
         default_index = index;
      index++;
   }

   if (default_index >= 0)
      gtk_combo_box_set_active(GTK_COMBO_BOX(combo), default_index);

   g_free(list);
}

GtkWidget *create_codec_selector_vbox(GtkComboBoxText **out_tx, GtkComboBoxText **out_rx) {
   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *widget_label = gtk_label_new("TX/RX Codecs");

   tx_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
   rx_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());

   CodecSelectorCtx *tx_ctx = g_new0(CodecSelectorCtx, 1);
   CodecSelectorCtx *rx_ctx = g_new0(CodecSelectorCtx, 1);
   tx_ctx->conn = ws_conn;
   tx_ctx->is_tx = true;
   rx_ctx->conn = ws_conn;
   rx_ctx->is_tx = false;

   gtk_combo_box_set_active(GTK_COMBO_BOX(tx_combo), 1);
   gtk_combo_box_set_active(GTK_COMBO_BOX(rx_combo), 1);

   gtk_box_pack_start(GTK_BOX(vbox), widget_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(tx_combo), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(rx_combo), FALSE, FALSE, 0);

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
