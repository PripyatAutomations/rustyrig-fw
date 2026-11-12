// Waveform recording playback panel. GPL-2.0-only.
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <rrclient/audio.h>
#include <rrclient/recordings.h>
#include <rrclient/gtk/gtk.floating-panel.h>
#include <rrclient/gtk/gtk.core.h>
#include <librustyaxe/event-bus.h>
#include "gtk.recording-player.h"

extern GtkWidget *main_window, *main_notebook;
static struct {
   RRFloatPanel *panel;
   GtkWidget *wave, *status, *position_label, *reverse, *play, *pause, *stop, *title;
   char *path;
   rr_recording_waveform *envelope;
   double position;
   gboolean playing, paused, deleting;
   rr_event_token_t wave_token, position_token, finished_token, error_token;
} player;

static gboolean allowed_recording(const char *path) {
   if (!path || g_file_test(path, G_FILE_TEST_IS_SYMLINK) ||
       !g_file_test(path, G_FILE_TEST_IS_REGULAR)) return FALSE;
   if (!g_str_has_suffix(path, ".ogg") && !g_str_has_suffix(path, ".flac")) return FALSE;
   char *root = rr_recordings_directory();
   char *canonical_root = g_canonicalize_filename(root, NULL);
   char *canonical_path = g_canonicalize_filename(path, NULL);
   size_t length = strlen(canonical_root);
   gboolean allowed = g_str_has_prefix(canonical_path, canonical_root) &&
      canonical_path[length] == G_DIR_SEPARATOR;
   g_free(root);
   g_free(canonical_root);
   g_free(canonical_path);
   return allowed;
}
static void playback_request(char operation, double seconds) {
   rr_audio_recording_control_t cmd = {0};
   cmd.operation = operation;
   cmd.seconds = seconds;
   cmd.reverse = player.reverse && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(player.reverse));
   if (player.path) g_strlcpy(cmd.path, player.path, sizeof(cmd.path));
   event_emit_binary(RR_AUDIO_RECORDING_CONTROL_REQUEST, NULL, &cmd, sizeof(cmd));
}
static char *timecode(double seconds) {
   guint total = (guint)MAX(0.0, seconds);
   return g_strdup_printf("%02u:%02u:%02u", total / 3600,
                          (total / 60) % 60, total % 60);
}
static void update_position(void) {
   if (!player.position_label) return;
   char *pos = timecode(player.position);
   char *end = timecode(player.envelope ? player.envelope->duration : 0.0);
   char *label = g_strdup_printf("%s / %s", pos, end);
   gtk_label_set_text(GTK_LABEL(player.position_label), label);
   g_free(pos); g_free(end); g_free(label);
   if (player.wave) gtk_widget_queue_draw(player.wave);
}
static void status(const char *message) {
   if (player.status) gtk_label_set_text(GTK_LABEL(player.status), message);
}
static void controls_update(void) {
   if (player.play) gtk_widget_set_sensitive(player.play, player.envelope && player.envelope->success);
   if (player.pause) gtk_widget_set_sensitive(player.pause, player.playing && !player.paused);
   if (player.stop) gtk_widget_set_sensitive(player.stop, player.playing);
}
static void start_from(double seconds) {
   if (!player.envelope || !player.envelope->success || !player.path) return;
   player.position = CLAMP(seconds, 0.0, player.envelope->duration);
   playback_request('P', player.position);
   player.playing = TRUE;
   player.paused = FALSE;
   status("Playing through the client PCM hub");
   controls_update();
   update_position();
}
static gboolean draw_wave(GtkWidget *widget, cairo_t *cr, gpointer unused) {
   (void)unused;
   double w = gtk_widget_get_allocated_width(widget);
   double h = gtk_widget_get_allocated_height(widget);
   if (w <= 1 || h <= 1) return FALSE;
   GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
   gtk_render_background(ctx, cr, 0, 0, w, h);
   GdkRGBA ink, accent;
   gtk_style_context_get_color(ctx, GTK_STATE_FLAG_NORMAL, &ink);
   gtk_style_context_get_background_color(ctx, GTK_STATE_FLAG_SELECTED, &accent);
   if (accent.alpha <= 0.01) accent = ink;
   cairo_set_line_width(cr, 1);
   for (unsigned channel = 0; channel < 2; channel++) {
      double axis = h * (channel ? 0.75 : 0.25);
      cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, 0.21);
      cairo_move_to(cr, 0, axis + 0.5);
      cairo_line_to(cr, w, axis + 0.5);
      cairo_stroke(cr);
      if (!player.envelope || !player.envelope->count) continue;
      const rr_recording_waveform *wave = player.envelope;
      cairo_set_source_rgba(cr, accent.red, accent.green, accent.blue, 0.95);
      for (gint x = 0; x < (gint)w; x++) {
         guint from = (guint)((double)x * wave->count / w);
         guint to = (guint)((double)(x + 1) * wave->count / w);
         if (to <= from) to = from + 1;
         from = MIN(from, wave->count - 1);
         to = MIN(to, wave->count);
         gint16 lo = G_MAXINT16, hi = G_MININT16;
         for (guint i = from; i < to; i++) {
            const rr_recording_wave_bin *b = &wave->bins[i];
            lo = MIN(lo, channel ? b->right_low : b->left_low);
            hi = MAX(hi, channel ? b->right_high : b->left_high);
         }
         double top = axis - ((double)hi / 32768.0) * (h * 0.21);
         double bottom = axis - ((double)lo / 32768.0) * (h * 0.21);
         cairo_move_to(cr, x + 0.5, top);
         cairo_line_to(cr, x + 0.5, bottom);
      }
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, 0.8);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 11);
      cairo_move_to(cr, 8, channel ? h * 0.58 : 15);
      cairo_show_text(cr, channel ? "R" : "L");
   }
   if (player.envelope && player.envelope->duration > 0.0) {
      double cursor_x = CLAMP(player.position / player.envelope->duration, 0.0, 1.0) * w;
      cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, 0.10);
      cairo_rectangle(cr, 0, 0, cursor_x, h);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, accent.red, accent.green, accent.blue, 1.0);
      cairo_set_line_width(cr, 2);
      cairo_move_to(cr, cursor_x, 0);
      cairo_line_to(cr, cursor_x, h);
      cairo_stroke(cr);
   } else {
      cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, 0.8);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      cairo_move_to(cr, 14, h * 0.5);
      cairo_show_text(cr, "Analyzing stereo waveform…");
   }
   return FALSE;
}
static gboolean wave_click(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)unused;
   if (event->button != 1 || !player.envelope || !player.envelope->success) return FALSE;
   gint width = gtk_widget_get_allocated_width(widget);
   if (width <= 0) return FALSE;
   double seconds = CLAMP(event->x / width, 0.0, 1.0) * player.envelope->duration;
   start_from(seconds);
   return TRUE;
}
static void play_clicked(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   if (player.playing && player.paused) {
      playback_request('R', player.position);
      player.paused = FALSE;
      status("Playing through the client PCM hub");
      controls_update();
      return;
   }
   if (player.envelope && player.envelope->success) {
      double at = player.position;
      if (at >= player.envelope->duration &&
          !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(player.reverse))) at = 0;
      if (at <= 0 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(player.reverse)))
         at = player.envelope->duration;
      start_from(at);
   }
}
static void pause_clicked(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   if (!player.playing || player.paused) return;
   playback_request('H', player.position);
   player.paused = TRUE;
   status("Paused");
   controls_update();
}
static void stop_clicked(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   if (!player.playing) return;
   playback_request('S', player.position);
   player.playing = player.paused = FALSE;
   status("Stopping playback…");
   controls_update();
}
static void reverse_changed(GtkToggleButton *button, gpointer unused) {
   (void)unused;
   if (!player.envelope || !player.envelope->success) return;
   if (player.playing) start_from(player.position);
   else {
      player.position = gtk_toggle_button_get_active(button) ? player.envelope->duration : 0.0;
      update_position();
   }
}
static void deletion_complete(void) {
   if (!allowed_recording(player.path)) return;
   if (g_unlink(player.path) != 0) {
      status("Unable to delete recording");
      return;
   }
   event_emit("client.recordings.refresh", NULL, NULL);
   status("Recording deleted");
   if (player.panel) rr_floating_panel_destroy(player.panel);
}
static void delete_clicked(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   if (!player.path) return;
   char *base = g_path_get_basename(player.path);
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
      GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
      "Permanently delete '%s'?", base);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
      "This deletes the selected file, not other recording formats.");
   gboolean confirm = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
   gtk_widget_destroy(dialog);
   g_free(base);
   if (!confirm) return;
   if (player.playing) {
      player.deleting = TRUE;
      playback_request('S', player.position);
      status("Stopping playback before deleting…");
   } else deletion_complete();
}
static void waveform_ready(const char *event, const void *data, size_t size,
                            rrconn_t *conn, void *unused) {
   (void)event; (void)conn; (void)unused;
   if (!player.panel || !data || size != sizeof(rr_recording_waveform)) return;
   const rr_recording_waveform *result = data;
   if (!memchr(result->path, 0, sizeof(result->path)) ||
       g_strcmp0(result->path, player.path)) return;
   if (!player.envelope) player.envelope = g_new0(rr_recording_waveform, 1);
   memcpy(player.envelope, result, sizeof(*result));
   if (result->success) status("Click anywhere in the waveform to seek and play");
   else status("Unable to analyze waveform (unsupported or damaged Ogg/FLAC file)");
   player.position = 0.0;
   if (player.reverse && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(player.reverse)))
      player.position = result->duration;
   controls_update();
   update_position();
}
static void position_changed(const char *event, const char *value,
                             rrconn_t *conn, void *unused) {
   (void)event; (void)conn; (void)unused;
   if (!player.panel || !player.playing || !value || !player.envelope) return;
   char *end = NULL;
   double seconds = g_ascii_strtod(value, &end);
   if (end == value || !isfinite(seconds)) return;
   player.position = CLAMP(seconds, 0.0, player.envelope->duration);
   update_position();
}
static void playback_finished(const char *event, const char *payload,
                              rrconn_t *conn, void *unused) {
   (void)event; (void)payload; (void)conn; (void)unused;
   if (!player.panel) return;
   player.playing = player.paused = FALSE;
   if (player.deleting) {
      player.deleting = FALSE;
      deletion_complete();
      return;
   }
   /* The completion event now means the PCM mixer has drained the tail. */
   player.position = (player.reverse && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(player.reverse))) ?
      (player.envelope ? player.envelope->duration : 0.0) : 0.0;
   status("Playback finished");
   controls_update();
   update_position();
}
static void playback_error(const char *event, const char *message,
                           rrconn_t *conn, void *unused) {
   (void)event; (void)conn; (void)unused;
   if (!player.panel) return;
   player.playing = player.paused = FALSE;
   status(message ? message : "Playback unavailable");
   controls_update();
}
static void close_panel(gpointer unused) {
   (void)unused;
   if (player.panel) rr_floating_panel_destroy(player.panel);
}
static void destroyed(GtkWidget *widget, gpointer unused) {
   (void)widget; (void)unused;
   if (player.playing) playback_request('S', player.position);
   player.panel = NULL;
   player.wave = player.status = player.position_label = player.title = NULL;
   player.reverse = player.play = player.pause = player.stop = NULL;
   player.playing = player.paused = player.deleting = FALSE;
   g_clear_pointer(&player.path, g_free);
   g_clear_pointer(&player.envelope, g_free);
}
void gtk_recording_player_show(const char *path) {
   if (!allowed_recording(path)) return;
   if (player.panel) {
      if (g_strcmp0(player.path, path)) {
         if (player.playing) playback_request('S', player.position);
         g_free(player.path);
         player.path = g_strdup(path);
         if (player.title) {
            char *base = g_path_get_basename(path);
            gtk_label_set_text(GTK_LABEL(player.title), base);
            gtk_widget_set_tooltip_text(player.title, path);
            g_free(base);
         }
         g_clear_pointer(&player.envelope, g_free);
         player.position = 0;
         player.playing = player.paused = FALSE;
         status("Analyzing waveform…");
         rr_recordings_waveform_request(path);
      }
      rr_floating_panel_restore(player.panel);
      rr_floating_panel_show(player.panel);
      update_position();
      return;
   }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;
   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) return;
   player.panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook),
      "recording-player", "Recording Playback");
   if (!player.panel) return;
   player.path = g_strdup(path);
   rr_floating_panel_set_close_callback(player.panel, close_panel, NULL);
   GtkWidget *body = rr_floating_panel_body(player.panel);
   gtk_widget_set_size_request(body, 820, 360);
   char *basename = g_path_get_basename(path);
   GtkWidget *title = gtk_label_new(basename);
   player.title = title;
   gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
   gtk_label_set_xalign(GTK_LABEL(title), 0);
   gtk_widget_set_tooltip_text(title, path);
   gtk_box_pack_start(GTK_BOX(body), title, FALSE, FALSE, 0);
   g_free(basename);
   player.wave = gtk_drawing_area_new();
   gtk_widget_set_size_request(player.wave, -1, 210);
   gtk_widget_set_hexpand(player.wave, TRUE);
   gtk_widget_set_vexpand(player.wave, TRUE);
   gtk_widget_add_events(player.wave, GDK_BUTTON_PRESS_MASK);
   gtk_widget_set_tooltip_text(player.wave, "Click anywhere to seek and play from that point");
   g_signal_connect(player.wave, "draw", G_CALLBACK(draw_wave), NULL);
   g_signal_connect(player.wave, "button-press-event", G_CALLBACK(wave_click), NULL);
   gtk_box_pack_start(GTK_BOX(body), player.wave, TRUE, TRUE, 0);
   GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(body), controls, FALSE, FALSE, 0);
   player.play = gtk_button_new_with_label("Play");
   player.pause = gtk_button_new_with_label("Pause");
   player.stop = gtk_button_new_with_label("Stop");
   g_signal_connect(player.play, "clicked", G_CALLBACK(play_clicked), NULL);
   g_signal_connect(player.pause, "clicked", G_CALLBACK(pause_clicked), NULL);
   g_signal_connect(player.stop, "clicked", G_CALLBACK(stop_clicked), NULL);
   gtk_box_pack_start(GTK_BOX(controls), player.play, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(controls), player.pause, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(controls), player.stop, FALSE, FALSE, 0);
   player.reverse = gtk_check_button_new_with_label("Reverse");
   gtk_widget_set_tooltip_text(player.reverse, "Decode and play in reverse, without loading the whole file into memory");
   g_signal_connect(player.reverse, "toggled", G_CALLBACK(reverse_changed), NULL);
   gtk_box_pack_start(GTK_BOX(controls), player.reverse, FALSE, FALSE, 6);
   player.position_label = gtk_label_new("00:00:00 / 00:00:00");
   gtk_box_pack_start(GTK_BOX(controls), player.position_label, TRUE, TRUE, 0);
   GtkWidget *delete_btn = gtk_button_new_with_label("Delete…");
   gtk_widget_set_tooltip_text(delete_btn, "Permanently delete this recording, with confirmation");
   g_signal_connect(delete_btn, "clicked", G_CALLBACK(delete_clicked), NULL);
   gtk_box_pack_end(GTK_BOX(controls), delete_btn, FALSE, FALSE, 0);
   player.status = gtk_label_new("Analyzing waveform…");
   gtk_label_set_xalign(GTK_LABEL(player.status), 0);
   gtk_box_pack_start(GTK_BOX(body), player.status, FALSE, FALSE, 0);
   g_signal_connect(rr_floating_panel_widget(player.panel), "destroy",
      G_CALLBACK(destroyed), NULL);
   gtk_widget_show_all(body);
   rr_floating_panel_show(player.panel);
   controls_update();
   rr_recordings_waveform_request(path);
}
void gtk_recording_player_init(void) {
   player.wave_token = event_on_binary_domain_token(RR_RECORDING_WAVEFORM_READY,
      waveform_ready, NULL, "gtk");
   player.position_token = event_on_domain_token(RR_RECORDING_PLAYBACK_POSITION,
      position_changed, NULL, "gtk");
   player.finished_token = event_on_domain_token("client.recordings.playback-finished",
      playback_finished, NULL, "gtk");
   player.error_token = event_on_domain_token("client.recordings.playback-error",
      playback_error, NULL, "gtk");
}
void gtk_recording_player_shutdown(void) {
   event_off_token(player.wave_token);
   event_off_token(player.position_token);
   event_off_token(player.finished_token);
   event_off_token(player.error_token);
   player.wave_token = player.position_token = player.finished_token = player.error_token = NULL;
   if (player.panel) rr_floating_panel_destroy(player.panel);
}
