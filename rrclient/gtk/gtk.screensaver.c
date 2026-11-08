// GTK screensaver idle state and gmatrix launcher.
// Part of rustyrig-fw; GPL-2.0-only.
#include "gtk.screensaver.h"
#include "gtk.core.h"
#include <librustyaxe/core.h>
#include <glib.h>
#include <string.h>

static gint64 screensaver_last_activity;
static GPid screensaver_pid;
static gint screensaver_pointer_x, screensaver_pointer_y;
static GdkScreen *screensaver_pointer_screen;

static void screensaver_child_exited(GPid pid, gint status, gpointer data) {
   (void)status;
   (void)data;

   if (screensaver_pid == pid) {
      screensaver_pid = 0;
   }
   g_spawn_close_pid(pid);
   screensaver_last_activity = g_get_monotonic_time();
}

void gtk_screensaver_activity(void) {
   screensaver_last_activity = g_get_monotonic_time();
}
gboolean gtk_screensaver_activity_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
   (void)widget;
   (void)data;

   if (event->type == GDK_KEY_PRESS || event->type == GDK_BUTTON_PRESS || event->type == GDK_SCROLL || event->type == GDK_MOTION_NOTIFY) {
      gtk_screensaver_activity();
   }

   return FALSE;
}

/* Never start gmatrix unless the main window itself has focus and occupies the display as a maximized or fullscreen window. */
static gboolean screensaver_window_eligible(void) {
   if (!main_window || !GTK_IS_WINDOW(main_window) || !gtk_window_has_toplevel_focus(GTK_WINDOW(main_window))) {
      return FALSE;
   }
   GdkWindow *window = gtk_widget_get_window(main_window);

   if (!window) {
      return FALSE;
   }
   GdkWindowState state = gdk_window_get_state(window);

   return (state & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)) != 0 && !(state & GDK_WINDOW_STATE_ICONIFIED);
}

static gboolean screensaver_launch(void) {
   if (!screensaver_window_eligible() || screensaver_pid) {
      return FALSE;
   }
   gchar program[] = "gmatrix";
   gchar *argv[] = {
      program, NULL
   };
   GError *error = NULL;
   GPid pid = 0;

   if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error)) {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to launch gmatrix: %s", error->message);
      g_clear_error(&error);
      gtk_screensaver_activity();

      return FALSE;
   }
   screensaver_pid = pid;
   g_child_watch_add(pid, screensaver_child_exited, NULL);
   gtk_screensaver_activity();

   return TRUE;
}
gboolean gtk_screensaver_idle_check(gpointer unused) {
   (void)unused;

   if (!main_window || !GTK_IS_WINDOW(main_window)) {
      return G_SOURCE_CONTINUE;
   }
   /* Pointer movement, even over nested GTK controls, counts as activity. */
   GdkDisplay *display = gtk_widget_get_display(main_window);
   GdkSeat *seat = display ? gdk_display_get_default_seat(display) : NULL;
   GdkDevice *pointer = seat ? gdk_seat_get_pointer(seat) : NULL;

   if (pointer) {
      gint x, y;
      GdkScreen *screen = NULL;
      gdk_device_get_position(pointer, &screen, &x, &y);

      if (x != screensaver_pointer_x || y != screensaver_pointer_y || screen != screensaver_pointer_screen) {
         screensaver_pointer_x = x;
         screensaver_pointer_y = y;
         screensaver_pointer_screen = screen;
         gtk_screensaver_activity();
      }
   }
   int timeout = cfg_get_int("ui.screensaver.timeout", 3600);
   const char *selected = cfg_get("ui.screensaver");

   if (!selected || strcmp(selected, "matrix") || timeout <= 0 || screensaver_pid || !screensaver_window_eligible()) {
      gtk_screensaver_activity();

      return G_SOURCE_CONTINUE;
   }

   if (g_get_monotonic_time() - screensaver_last_activity >= (gint64) timeout * G_USEC_PER_SEC) {
      screensaver_launch();
   }

   return G_SOURCE_CONTINUE;
}

/* Spawn the independently packaged screensaver; never run it in the rrclient widget tree. GTK owns the Escape-prefix event handling here. */
gboolean gtk_screensaver_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)widget;
   (void)data;
   static gint64 escape_at;
   gint64 now = g_get_monotonic_time();

   if (event->keyval == GDK_KEY_Escape) {
      escape_at = now;

      return FALSE;
   }
   gboolean armed = escape_at && now - escape_at <= 1500000;
   escape_at = 0;

   if (!armed || (event->keyval != GDK_KEY_grave && event->keyval != GDK_KEY_asciitilde) ||
      (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK))) {
      return FALSE;
   }
   screensaver_launch();

   return TRUE;
}
