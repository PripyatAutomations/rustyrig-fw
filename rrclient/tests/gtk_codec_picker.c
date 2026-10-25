// Exercise the GTK change callback used by real codec picker selections.
#include <assert.h>
#include <stdarg.h>
#include "rrclient/gtk/gtk.codecpicker.c"
static rrconn_t connection;
rrconn_t *ws_conn = &connection, *ws_tx_conn;
static struct rr_client_media_chan channel = {
   .uuid = "stream-rx", .room = "#rplywv00-rig0"
};

static bool fail_select, selected_context = true;
static unsigned selections;
static const char *confirmed_rx = "pc16", *confirmed_tx = "mu08";
static const char *negotiated_codecs = "pc16 mu08 g722 opus";
bool rrclient_context_is_selected(void) {
   return selected_context;
}
const char *media_get_common_codecs(void) {
   return negotiated_codecs;
}
const char *rrclient_media_current_codec(bool tx) {
   return tx ? confirmed_tx : confirmed_rx;
}

static char reply_room[128], reply[1024];
const struct rr_client_media_chan *rrclient_media_codec_target_channel(bool is_tx) {
   return &channel;
}
bool rrclient_media_select_codec_channel(rrconn_t *cptr, bool is_tx, const char *codec, const char *uuid) {
   assert(uuid && !strcmp(uuid, "stream-rx"));
   assert(cptr == &connection && !is_tx && !strcmp(codec, "pc1P"));
   selections++;

   return fail_select;
}
void rrclient_media_description(const char *uuid, char *out, size_t capacity) {
   assert(!strcmp(uuid, "stream-rx"));
   snprintf(out, capacity, "rplywv00.rig0.rx (codec=pc16; uuid=stream-rx)");
}
const char *ui_active_window_name(void) {
   return "status";
}
bool ui_print(const char *room, const char *format, ...) {
   snprintf(reply_room, sizeof(reply_room), "%s", room);
   va_list args;
   va_start(args, format);
   vsnprintf(reply, sizeof(reply), format, args);
   va_end(args);

   return false;
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   GtkWidget *combo = gtk_combo_box_text_new();
   g_object_ref_sink(combo);
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "pc1P", "pc1P");
   gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
   CodecSelectorCtx ctx = {
      .is_tx = false
   };
   codec_changed_cb(GTK_COMBO_BOX_TEXT(combo), &ctx);
   assert(!strcmp(reply_room, "#rplywv00-rig0"));
   assert(strstr(reply, "Requested RX codec pc1P for rplywv00.rig0.rx"));
   reply[0] = 0;
   fail_select = true;
   codec_changed_cb(GTK_COMBO_BOX_TEXT(combo), &ctx);
   assert(!reply[0]);
   g_object_unref(combo);
   puts("PASS: GTK codec picker routes successful requests to the media room");
   rx_combo = gtk_combo_box_text_new();
   tx_combo = gtk_combo_box_text_new();
   g_object_ref_sink(rx_combo);
   g_object_ref_sink(tx_combo);
   CodecSelectorCtx tx_ctx = {
      .is_tx = true
   };
   g_signal_connect(rx_combo, "changed", G_CALLBACK(codec_changed_cb), &ctx);
   g_signal_connect(tx_combo, "changed", G_CALLBACK(codec_changed_cb), &tx_ctx);
   unsigned before = selections;
   codec_pickers_refresh();
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(rx_combo)), "pc16"));
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(tx_combo)), "mu08"));
   // A retained rig tab can have a different pair with an identical codec list.
   confirmed_rx = "opus";
   confirmed_tx = "g722";
   codec_pickers_refresh();
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(rx_combo)), "opus"));
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(tx_combo)), "g722"));
   // A different server can negotiate an entirely different list or NONE.
   negotiated_codecs = "mu08";
   confirmed_rx = "mu08";
   confirmed_tx = NULL;
   codec_pickers_refresh();
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(rx_combo)), "mu08"));
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(tx_combo)), "none"));
   // A background server's negotiation must not repaint the selected widget.
   selected_context = false;
   confirmed_rx = "pc16";
   confirmed_tx = "opus";
   negotiated_codecs = "pc16 opus";
   codec_media_codecs_cb("media.codecs", "", &connection, NULL);
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(rx_combo)), "mu08"));
   selected_context = true;
   codec_media_codecs_cb("media.codecs", "", &connection, NULL);
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(rx_combo)), "pc16"));
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(tx_combo)), "opus"));
   updating_codecs = true;
   codec_picker_set_active(false, "opus");
   assert(updating_codecs);     // Nested programmatic updates preserve the guard.
   updating_codecs = false;
   assert(selections == before); // Refreshing displays never sends codec commands.
   g_object_unref(rx_combo);
   g_object_unref(tx_combo);
   rx_combo = tx_combo = NULL;
   puts("PASS: VFO codec refresh restores rig/server RX/TX observations, NONE and negotiated lists without command echoes or background overwrites");

   return 0;
}
