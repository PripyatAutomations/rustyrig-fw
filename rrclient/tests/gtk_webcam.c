// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include <assert.h>
#include "gtk_test_events.h"
#include <rrclient/gtk/gtk.webcam.c>
#include <librrprotocol/server.events.h>
bool dying;
rrconn_t *ws_conn;
GtkWidget *main_window, *main_notebook;
bool rr_module_token_add(rr_event_token_t token) {
   return token != NULL;
}

GtkWidget *gtk_tabs_panel_restore_add(GtkNotebook *notebook, const char *title, GCallback callback, gpointer data) {
   (void)notebook;
   (void)title;
   (void)callback;
   (void)data;

   return NULL;
}

void gtk_tabs_panel_restore_remove(GtkWidget *tab) {
   (void)tab;
}

bool ui_print(const char *window, const char *fmt, ...) {
   return false;
}

bool rrclient_media_subscribe_single_video(void) {
   return true;
}

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
   decoded_output = output;
   decoded_user = user;
   starts++;

   return 1;
}

bool fwdsp_write_channel_samples(const char codec[5], bool tx, const char *identity, const void *data, size_t len) {
   assert(((const uint8_t *)data)[0] == 0xff);
   decoded_output(identity, display_frame->str, display_frame->len, decoded_user);

   return true;
}

int fwdsp_codec_stop_channel_immediate(const char *codec, bool tx, const char *identity) {
   stops++;

   return 0;
}

bool media_send_unsubscribe(rrconn_t *client, const char *uuid) {
   unsubscribes++;

   return true;
}

static void drain(void) {
   while (g_main_context_pending(NULL)) {
      g_main_context_iteration(NULL, FALSE);
   }
}

/* The core now forwards owned image/status events to GTK, rather than
 * calling webcam_display_frame() with a cross-thread connection pointer. */
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   gtk_test_events_init();
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *overlay = gtk_overlay_new();
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(overlay), main_notebook);
   gtk_container_add(GTK_CONTAINER(main_window), overlay);
   gtk_widget_show_all(main_window);

   GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 160, 120);
   gdk_pixbuf_fill(image, 0x00ff00ff);
   gchar *jpeg = NULL;
   gsize len = 0;
   assert(gdk_pixbuf_save_to_buffer(image, &jpeg, &len, "jpeg", NULL, NULL));
   g_object_unref(image);

   gtk_webcam_show(true);
   assert(webcam_visible && webcam_win && webcam_image);
   guint64 epoch = webcam_epoch;
   webcam_status_t status = {.epoch = epoch, .open = true, .subscribed = true};
   g_strlcpy(status.uuid, "camera", sizeof(status.uuid));
   g_strlcpy(status.profiles, "h264.1 h264.5 h264.10 jpeg.5", sizeof(status.profiles));
   rr_event_message_t status_event = {
      .name = RR_WEBCAM_STATUS, .payload = &status, .length = sizeof(status)
   };
   webcam_status_event(&status_event, NULL);
   assert(gtk_combo_box_set_active_id(GTK_COMBO_BOX(webcam_codecs), "h264"));
   assert(gtk_combo_box_set_active_id(GTK_COMBO_BOX(webcam_rates), "h264.10"));

   webcam_image_t *frame = g_malloc(sizeof(*frame) + len);
   frame->epoch = epoch - 1;
   memcpy(frame->bytes, jpeg, len);
   rr_event_message_t frame_event = {
      .name = RR_WEBCAM_IMAGE, .payload = frame, .length = sizeof(*frame) + len
   };
   webcam_image_event(&frame_event, NULL);
   assert(!webcam_pending && !webcam_idle); /* stale session ignored */
   frame->epoch = epoch;
   for (int n = 0 ; n < 100 ; n++)
      webcam_image_event(&frame_event, NULL);
   assert(webcam_idle && webcam_pending);
   guint pending = webcam_idle;
   webcam_image_event(&frame_event, NULL);
   assert(webcam_idle == pending); /* one scheduled GTK redraw */
   drain();
   GdkPixbuf *shown = gtk_image_get_pixbuf(GTK_IMAGE(webcam_image));
   assert(shown && gdk_pixbuf_get_width(shown) == 160 &&
      gdk_pixbuf_get_height(shown) == 120);
   assert(!webcam_pending && !webcam_idle);

   status.open = false;
   webcam_closed_event(&status_event, NULL);
   assert(!webcam_visible && !webcam_pending && !webcam_idle);
   free(frame);
   g_free(jpeg);
   gtk_widget_destroy(main_window);
   main_window = main_notebook = NULL;
   puts("PASS: owned webcam image events, session isolation, bounded GTK redraw and close cleanup");
   return 0;
}
