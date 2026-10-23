//
// rrclient/gtk/gtk.webcam.c: GTK video (webcam) viewer window
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Encoded video arrives with its stream and codec header. Configured fwdsp
// pipelines decode off-process to local image frames; GTK displays the newest
// image only. Codec/frame-rate profiles are advertised by the server.
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
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.winmgr.h>
#include "gtk.floating-panel.h"
#include <rrclient/media.h>
#include <libfwdspmgr/fwdsp-mgr.h>

extern rrconn_t *ws_conn;
extern bool ui_print(const char *window, const char *fmt, ...);

extern GtkWidget *main_window, *main_notebook;
static RRFloatPanel *webcam_panel;
static GtkWidget *webcam_win = NULL;
static GtkWidget *webcam_image = NULL;
/* Keep only the newest compressed frame; GTK owns all decoding and widgets. */
static GMutex webcam_lock;
static GBytes *webcam_pending;
static guint webcam_idle;
static bool webcam_visible;
static rrconn_t *webcam_connection;
static char webcam_decoder[5], webcam_uuid[64], webcam_profiles[512];
static GtkWidget *webcam_codecs, *webcam_rates;

void gtk_webcam_show(bool show);
static void webcam_decoded(const char *name, const void *data, size_t len, void *user);
static void webcam_panel_close(gpointer data) { (void)data; gtk_webcam_show(false); }


static gboolean webcam_blit_cb(gpointer user) {
   (void)user;
   g_mutex_lock(&webcam_lock);
   GBytes *frame = webcam_pending;
   webcam_pending = NULL;
   webcam_idle = 0;
   g_mutex_unlock(&webcam_lock);

   if (!frame) {
      return G_SOURCE_REMOVE;
   }

   if (webcam_image) {
      GError *error = NULL;
      GInputStream *stream = g_memory_input_stream_new_from_bytes(frame);
      GdkPixbuf *image = gdk_pixbuf_new_from_stream(stream, NULL, &error);
      g_object_unref(stream);

      if (image) {
         gtk_image_set_from_pixbuf(GTK_IMAGE(webcam_image), image);
         g_object_unref(image);
      } else if (error) {
         Log(LOG_DEBUG, "webcam", "Invalid display frame: %s", error->message);
         g_error_free(error);
      }
   }
   g_bytes_unref(frame);

   return G_SOURCE_REMOVE;
}

static void webcam_display_frame(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (dying || !data || len < 3 || len > 16 * 1024 * 1024) {
      return;
   }
   g_mutex_lock(&webcam_lock);

   if (webcam_visible && cptr == webcam_connection) {
      if (webcam_pending) {
         g_bytes_unref(webcam_pending);
      }
      webcam_pending = g_bytes_new(data, len);

      if (!webcam_idle) {
         webcam_idle = g_idle_add(webcam_blit_cb, NULL);
      }
   }
   g_mutex_unlock(&webcam_lock);
}

static void webcam_decoded(const char *name, const void *data, size_t len, void *user) {
   webcam_display_frame(NULL, data, len, webcam_connection, NULL);
}

static void webcam_frame_handler(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user) {
   struct rr_binframe frame;

   if (!webcam_visible || cptr != webcam_connection || !data ||
      rr_binframe_parse(data, len, &frame) || len != RR_BINFRAME_HDR_LEN + frame.len ||
      frame.hdr.subsystem != RR_BINFRAME_SUBSYS_VIDEO || frame.hdr.direction != RR_BINFRAME_DIR_RX) {
      return;
   }
   char codec[5] = {
      0
   };
   memcpy(codec, frame.hdr.codec, 4);

   if (strcmp(codec, webcam_decoder)) {
      if (*webcam_decoder) {
         fwdsp_codec_stop_channel_immediate(webcam_decoder, false, "webcam-viewer");
      }
      webcam_decoder[0] = '\0';

      if (fwdsp_video_pipeline_start(codec, "webcam-viewer", false, webcam_decoded, NULL) < 0) {
         return;
      }
      snprintf(webcam_decoder, sizeof(webcam_decoder), "%s", codec);
   }

   if (!fwdsp_write_channel_samples(codec, false, "webcam-viewer", frame.data, frame.len)) {
      webcam_decoder[0] = '\0';
   }
}

/* PARITY: rustyrig-www/js/webui.media.js webcam profile FPS ordering. */
static gint webcam_rate_compare(gconstpointer left, gconstpointer right) {
   guint64 a = g_ascii_strtoull((const char *)left + 5, NULL, 10);
   guint64 b = g_ascii_strtoull((const char *)right + 5, NULL, 10);

   return a < b ? -1 : a > b ? 1 : strcmp(left, right);
}

static void webcam_codec_changed(GtkComboBox *combo, gpointer user) {
   const char *codec = gtk_combo_box_get_active_id(combo);
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(webcam_rates));

   if (!codec || !*codec) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(webcam_rates), "", "Source rate");
   } else {
      char copy[sizeof(webcam_profiles)];
      snprintf(copy, sizeof(copy), "%s", webcam_profiles);
      char *save = NULL;
      GList *rates = NULL;

      for (char *name = strtok_r(copy, " ", &save) ; name ; name = strtok_r(NULL, " ", &save)) {
         if (!strncmp(name, codec, 4) && name[4] == '.') {
            rates = g_list_prepend(rates, name);
         }
      }

      rates = g_list_sort(rates, webcam_rate_compare);

      for (GList *item = rates ; item ; item = item->next) {
         const char *name = item->data;
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(webcam_rates), name, name + 5);
      }

      g_list_free(rates);
   }
   gtk_combo_box_set_active(GTK_COMBO_BOX(webcam_rates), 0);
}
static void webcam_apply(GtkButton *button, gpointer user) {
   const char *profile = gtk_combo_box_get_active_id(GTK_COMBO_BOX(webcam_rates));

   if (!profile || !webcam_connection || !*webcam_uuid) {
      return;
   }
   dict *message = dict_new();
   dict_add(message, "msg.type", "media");
   dict_add(message, "media.cmd", "profile");
   dict_add(message, "media.chan-uuid", webcam_uuid);
   dict_add(message, "media.profile", profile);
   ws_send_dict(NULL, webcam_connection, message, WEBSOCKET_OP_TEXT);
   dict_free(message);
}
static void webcam_controls_refresh(void) {
   webcam_profiles[0] = webcam_uuid[0] = '\0';
   rrclient_media_video_info(webcam_uuid, sizeof(webcam_uuid), webcam_profiles, sizeof(webcam_profiles));
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(webcam_codecs));
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(webcam_codecs), "", "Source codec");
   char copy[sizeof(webcam_profiles)], seen[256] = " ";
   snprintf(copy, sizeof(copy), "%s", webcam_profiles);
   char *save = NULL;

   for (char *name = strtok_r(copy, " ", &save) ; name ; name = strtok_r(NULL, " ", &save)) {
      if (strlen(name) < 6 || name[4] != '.') {
         continue;
      }
      name[4] = '\0';
      char token[8];
      snprintf(token, sizeof(token), " %s ", name);

      if (!strstr(seen, token)) {
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(webcam_codecs), name, name);
         strlcat(seen, name, sizeof(seen));
         strlcat(seen, " ", sizeof(seen));
      }
   }

   gtk_combo_box_set_active(GTK_COMBO_BOX(webcam_codecs), 0);
}

// Called when the viewer window is destroyed
static void webcam_destroy_cb(GtkWidget *widget, gpointer user) {
   (void)widget;
   (void)user;

   if (webcam_connection && *webcam_uuid) {
      media_send_unsubscribe(webcam_connection, webcam_uuid);
   }

   if (*webcam_decoder) {
      fwdsp_codec_stop_channel_immediate(webcam_decoder, false, "webcam-viewer");
   }
   webcam_decoder[0] = '\0';
   webcam_image = NULL;
   webcam_win = NULL;
   if (webcam_panel) {
      GtkWidget *body = rr_floating_panel_body(webcam_panel);
      GList *children = gtk_container_get_children(GTK_CONTAINER(body));
      for (GList *it = children; it; it = it->next) gtk_widget_destroy(GTK_WIDGET(it->data));
      g_list_free(children);
   }

   g_mutex_lock(&webcam_lock);
   webcam_visible = false;
   webcam_connection = NULL;

   if (webcam_idle) {
      g_source_remove(webcam_idle);
      webcam_idle = 0;
   }

   if (webcam_pending) {
      g_bytes_unref(webcam_pending);
      webcam_pending = NULL;
   }
   g_mutex_unlock(&webcam_lock);
}

// Show or hide the viewer window; frames are only decoded when it exists.
void gtk_webcam_show(bool show) {
   if (!show) {
      if (webcam_panel) rr_floating_panel_hide(webcam_panel);
      webcam_destroy_cb(NULL, NULL);
      return;
   }

   if (webcam_win) {
      if (webcam_connection && webcam_connection != ws_conn && *webcam_uuid) {
         media_send_unsubscribe(webcam_connection, webcam_uuid);
      }

      if (webcam_connection != ws_conn && *webcam_decoder) {
         fwdsp_codec_stop_channel_immediate(webcam_decoder, false, "webcam-viewer");
         webcam_decoder[0] = '\0';
      }
      g_mutex_lock(&webcam_lock);
      webcam_connection = ws_conn;
      webcam_visible = true;
      g_mutex_unlock(&webcam_lock);
      webcam_controls_refresh();
      rrclient_media_subscribe_single_video();
      rr_floating_panel_restore(webcam_panel);
      rr_floating_panel_show(webcam_panel);

      return;
   }
   g_mutex_lock(&webcam_lock);
   webcam_visible = true;
   webcam_connection = ws_conn;
   g_mutex_unlock(&webcam_lock);
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;
   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) {
      webcam_destroy_cb(NULL, NULL);
      return;
   }
   if (!webcam_panel) {
      webcam_panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook), "webcam", "Webcam");
      rr_floating_panel_set_close_callback(webcam_panel, webcam_panel_close, NULL);
   }
   webcam_win = rr_floating_panel_widget(webcam_panel);
   gtk_widget_set_size_request(rr_floating_panel_body(webcam_panel), 640, 480);

   webcam_image = gtk_image_new();
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   webcam_codecs = gtk_combo_box_text_new();
   webcam_rates = gtk_combo_box_text_new();
   GtkWidget *apply = gtk_button_new_with_label("Apply");
   gtk_box_pack_start(GTK_BOX(controls), gtk_label_new("Codec"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(controls), webcam_codecs, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(controls), gtk_label_new("Frames/sec"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(controls), webcam_rates, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(controls), apply, FALSE, FALSE, 0);
   g_signal_connect(webcam_codecs, "changed", G_CALLBACK(webcam_codec_changed), NULL);
   g_signal_connect(apply, "clicked", G_CALLBACK(webcam_apply), NULL);
   webcam_controls_refresh();
   gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box), webcam_image, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(webcam_panel)), box, TRUE, TRUE, 0);
   rr_floating_panel_show(webcam_panel);

   if (rrclient_media_subscribe_single_video()) {
      ui_print(NULL, "\00311Webcam viewer open; subscribed to the only video stream\017");
   } else {
      ui_print(NULL, "\00311Webcam viewer open; subscribe to the video channel with \017/media SUB <uuid|#>\00311 to start the stream\017");
   }
}

static gboolean webcam_close_disconnected(gpointer user) {
   if (!webcam_connection) {
      gtk_webcam_show(false);
   }

   return G_SOURCE_REMOVE;
}
static void webcam_disconnected(const char *event, const char *data, rrconn_t *client, void *user) {
   g_mutex_lock(&webcam_lock);

   if (client && client == webcam_connection) {
      webcam_connection = NULL;
      webcam_visible = false;
      g_idle_add(webcam_close_disconnected, NULL);
   }
   g_mutex_unlock(&webcam_lock);
}

// Register the video frame listener (called from gtk init)
void rrclient_webcam_register(void) {
   /* The frontend is dlclosed before socket teardown; these callbacks must be removed by rr_module_shutdown() to avoid jumping into unmapped code. */
   extern bool rr_module_token_add(rr_event_token_t token);
   rr_module_token_add(event_on_binary_token(RR_VIDEO_FRAME_EVENT, webcam_frame_handler, NULL));
   rr_module_token_add(event_on_token("disconnected", webcam_disconnected, NULL));
}
