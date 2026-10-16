#include <assert.h>
#include <rrclient/gtk/gtk.webcam.c>
#include <librrprotocol/server.events.h>
bool dying;
rrconn_t *ws_conn;
bool ui_print(const char *window, const char *fmt, ...) { return false; }
bool rrclient_media_subscribe_single_video(void) { return true; }
bool rrclient_media_video_info(char *uuid, size_t uuid_size, char *profiles, size_t profiles_size) {
   snprintf(uuid, uuid_size, "camera");
   snprintf(profiles, profiles_size, "h264.1 h264.5 h264.10 jpeg.5");
   return true;
}

static GString *display_frame;
static fwdsp_processor_output_cb decoded_output;
static void *decoded_user;
static unsigned starts, stops, unsubscribes;
int fwdsp_video_pipeline_start(const char codec[5], const char *identity, bool capture, fwdsp_processor_output_cb output, void *user) {
   assert(!capture && !strcmp(codec, "jpeg") && !strcmp(identity, "webcam-viewer"));
   decoded_output = output; decoded_user = user; starts++;
   return 1;
}
bool fwdsp_write_channel_samples(const char codec[5], bool tx, const char *identity, const void *data, size_t len) {
   assert(((const uint8_t *)data)[0] == 0xff);
   decoded_output(identity, display_frame->str, display_frame->len, decoded_user);
   return true;
}
int fwdsp_codec_stop_channel_immediate(const char *codec, bool tx, const char *identity) { stops++; return 0; }
bool media_send_unsubscribe(rrconn_t *client, const char *uuid) { unsubscribes++; return true; }

static void drain(void) {
   while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
}

int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   event_init();
   rrconn_t first = {0}, other = {0};
   ws_conn = &first;
   GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 160, 120);
   gdk_pixbuf_fill(image, 0x00ff00ff);
   gchar *jpeg = NULL;
   gsize len = 0;
   assert(gdk_pixbuf_save_to_buffer(image, &jpeg, &len, "jpeg", NULL, NULL));
   display_frame = g_string_new("P6\n160 120\n255\n");
   g_string_append_len(display_frame, (char *)gdk_pixbuf_get_pixels(image), 160 * 120 * 3);
   g_object_unref(image);
   webcam_display_frame(NULL, jpeg, len, &first, NULL);
   assert(!webcam_pending && !webcam_idle);
   gtk_webcam_show(true);
   webcam_display_frame(NULL, jpeg, len, &other, NULL);
   assert(!webcam_pending && !webcam_idle);
   assert(gtk_combo_box_set_active_id(GTK_COMBO_BOX(webcam_codecs), "h264"));
   assert(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(webcam_rates)), NULL) == 3);
   assert(gtk_combo_box_set_active_id(GTK_COMBO_BOX(webcam_rates), "h264.10"));
   rrclient_webcam_register();
   uint8_t *wire = NULL;
   int size = rr_binframe_frame(&wire, RR_BINFRAME_SUBSYS_VIDEO, "jpeg", RR_BINFRAME_DIR_RX,
      RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA, 1, 1, 0, (uint8_t *)jpeg, len);
   assert(size > 0);
   for (int n = 0; n < 100; n++) assert(ws_binframe_process_client(&first, (char *)wire, size));
   assert(webcam_idle && webcam_pending);
   guint pending = webcam_idle;
   webcam_display_frame(NULL, jpeg, len, &first, NULL);
   assert(webcam_idle == pending); /* One idle, even if frames outpace the UI. */
   drain();
   GdkPixbuf *shown = gtk_image_get_pixbuf(GTK_IMAGE(webcam_image));
   assert(shown && gdk_pixbuf_get_width(shown) == 160 && gdk_pixbuf_get_height(shown) == 120);
   assert(!webcam_pending && !webcam_idle);
   webcam_display_frame(NULL, jpeg, len, &first, NULL);
   gtk_webcam_show(false);
   assert(!webcam_pending && !webcam_idle && !webcam_win);
   assert(starts == 1 && stops == 1 && unsubscribes == 1);
   drain();
   gtk_webcam_show(true);
   webcam_disconnected("disconnected", NULL, &first, NULL);
   assert(!webcam_connection && !webcam_visible);
   drain();
   assert(!webcam_win && unsubscribes == 1);
   free(wire);
   g_free(jpeg);
   g_string_free(display_frame, TRUE);
   puts("PASS: JPEG wire dispatch preserves server identity, bounded GTK rendering and closed-viewer cleanup");
}
