//
// rrclient/gtk.webcam.c: GTK video (webcam) viewer window
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Video frames arrive as SUBSYS_VIDEO binframes; ws_binframe_process() strips
// the header and rr_binframe_dispatch() emits the media.frame.video event with
// the raw payload (JPEG). We decode it and blit into a GtkImage in a viewer
// window. Subscribing happens via the media channel code (/media SUB n).
//
// PARITY: rustyrig-www/js/webui.media.js (video channel handling)
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <rrclient/gtk.core.h>
#include <rrclient/gtk.winmgr.h>

extern rrconn_t *ws_conn;
extern bool ui_print(const char *window, const char *fmt, ...);

static GtkWidget *webcam_win = NULL;
static GtkWidget *webcam_image = NULL;
static GdkPixbuf *webcam_frame = NULL;   // latest decoded frame

static gboolean webcam_blit_cb(gpointer user) {
   (void)user;

   if (!webcam_image || !webcam_frame) {
      return false;
   }
   gtk_image_set_from_pixbuf(GTK_IMAGE(webcam_image), webcam_frame);
   gtk_widget_queue_draw(webcam_image);

   return false;
}

// Decode a JPEG payload and blit it on the GTK main loop
static void webcam_decode(const uint8_t *data, size_t len) {
   GError *err = NULL;
   GInputStream *stream = g_memory_input_stream_new_from_data(data, len, NULL);
   GdkPixbuf *pix = gdk_pixbuf_new_from_stream(stream, NULL, &err);

   g_object_unref(stream);

   if (!pix) {
      if (err) {
         Log(LOG_DEBUG, "webcam", "Failed to decode %zu byte video frame: %s", len, err->message);
         g_error_free(err);
      }
      return;
   }
   if (webcam_frame) {
      g_object_unref(webcam_frame);
   }
   webcam_frame = pix;

   // Blit on the GTK thread
   gdk_threads_add_idle(webcam_blit_cb, NULL);
}

// media.frame.video binframes (PARITY: librrprotocol/binframe.c dispatch)
static void webcam_frame_handler(const char *event, const void *data, size_t len,
   rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;

   if (dying || !data || len == 0) {
      return;
   }
   webcam_decode( (const uint8_t *)data, len);
}

// Called when the viewer window is destroyed
static void webcam_destroy_cb(GtkWidget *widget, gpointer user) {
   (void)widget;
   (void)user;

   webcam_win = NULL;
   webcam_image = NULL;

   if (webcam_frame) {
      g_object_unref(webcam_frame);
      webcam_frame = NULL;
   }
}

// /webcam [SHOW|HIDE] - toggle the viewer window; frames are only decoded
// when the window exists (and we're subscribed to the channel)
bool cmd_webcam(int argc, char **args) {
   const char *sub = (argc > 1 ? args[1] : "SHOW");

   if (strcasecmp(sub, "HIDE") == 0 || strcasecmp(sub, "CLOSE") == 0) {
      if (webcam_win) {
         gtk_widget_destroy(webcam_win);   // destroy cb clears the state
      }
      return false;
   }
   if (webcam_win) {
      gtk_window_present(GTK_WINDOW(webcam_win) );
      return false;
   }
   webcam_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(webcam_win), "Webcam");
   gtk_window_set_default_size(GTK_WINDOW(webcam_win), 640, 480);
   g_signal_connect(webcam_win, "destroy", G_CALLBACK(webcam_destroy_cb), NULL);

   webcam_image = gtk_image_new();
   gtk_container_add(GTK_CONTAINER(webcam_win), webcam_image);
   gtk_widget_show_all(webcam_win);
   ui_print(NULL, "{bright-cyan}Webcam viewer open; subscribe to the video channel with {reset}/media SUB <uuid|#>{bright-cyan} to start the stream{reset}");

   return false;
}

// Register the video frame listener (called from gtk init)
void rrclient_webcam_register(void) {
   event_on_binary("media.frame.video", webcam_frame_handler, NULL);
}
