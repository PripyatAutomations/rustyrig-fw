// Exercise the GTK change callback used by real codec picker selections.
#include <assert.h>
#include <stdarg.h>
#include "rrclient/gtk/gtk.codecpicker.c"
static rrconn_t connection;
rrconn_t *ws_conn = &connection, *ws_tx_conn;
static struct rr_client_media_chan channel = {.uuid="stream-rx", .room="#rplywv00-rig0"};
static bool fail_select;
static char reply_room[128], reply[1024];
const struct rr_client_media_chan *rrclient_media_codec_target_channel(bool is_tx) { return &channel; }
bool rrclient_media_select_codec(rrconn_t *cptr, bool is_tx, const char *codec) {
   assert(cptr == &connection && !is_tx && !strcmp(codec, "pc1P"));
   return fail_select;
}
void rrclient_media_description(const char *uuid, char *out, size_t capacity) {
   assert(!strcmp(uuid, "stream-rx"));
   snprintf(out, capacity, "rplywv00.rig0.rx (codec=pc16; uuid=stream-rx)");
}
const char *ui_active_window_name(void) { return "status"; }
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
   CodecSelectorCtx ctx = {.is_tx=false};
   codec_changed_cb(GTK_COMBO_BOX_TEXT(combo), &ctx);
   assert(!strcmp(reply_room, "#rplywv00-rig0"));
   assert(strstr(reply, "Requested RX codec pc1P for rplywv00.rig0.rx"));
   reply[0] = 0;
   fail_select = true;
   codec_changed_cb(GTK_COMBO_BOX_TEXT(combo), &ctx);
   assert(!reply[0]);
   g_object_unref(combo);
   puts("PASS: GTK codec picker routes successful requests to the media room");
   return 0;
}
