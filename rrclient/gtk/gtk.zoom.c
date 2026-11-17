// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// GTK interface zoom: font DPI, authored pixel CSS and widget geometry.
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.hotkey.h>

static unsigned zoom_percent = 100;
static int base_dpi;
static GtkCssProvider *zoom_theme_provider;
static GtkWidget *zoom_window, *zoom_viewport;
typedef struct zoom_touch {
   GtkGesture *gesture;
   GtkWidget *target;
   GdkEventSequence *points[2];
   double x[2], y[2], menu_x, menu_y;
   guint32 started;
   unsigned initial, finish_source, wait_ms;
   bool active, tap, pinching;
} zoom_touch_t;
static GList *touch_handlers;
static unsigned active_touches;
static void zoom_touch_bind(GtkWidget *window);
gboolean gtk_ui_zoom_key_release(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)widget;(void)event;(void)data;
   return FALSE;
}

void gtk_ui_zoom_shutdown(void) {
   while (touch_handlers) {
      zoom_touch_t *touch = touch_handlers->data;
      GtkWidget *window = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(touch->gesture));
      g_object_set_data(G_OBJECT(window), "rr-touch-zoom", NULL);
   }

   if (zoom_window) {
      g_object_remove_weak_pointer(G_OBJECT(zoom_window), (gpointer *)&zoom_window);
      zoom_window = NULL;
   }

   if (zoom_viewport) {
      g_object_remove_weak_pointer(G_OBJECT(zoom_viewport), (gpointer *) &zoom_viewport);
      zoom_viewport = NULL;
   }

   if (zoom_theme_provider) {
      gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(zoom_theme_provider));
      g_clear_object(&zoom_theme_provider);
   }

   if (map_hook) {
      g_signal_remove_emission_hook(map_signal, map_hook);
      map_hook = 0;
   }
}

gboolean gtk_ui_zoom_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)data;

   if (!event || event->type != GDK_KEY_PRESS) {
      return FALSE;
   }
   const GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();
   const bool control = (modifiers & GDK_CONTROL_MASK) != 0;
   const bool alt = (modifiers & GDK_MOD1_MASK) != 0;

   if (modifiers & ~(GDK_CONTROL_MASK | GDK_MOD1_MASK)) {
      return FALSE;
   }

   if (!control && !alt) {
      return false;
   }

   switch (event->keyval) {
      case GDK_KEY_0:
      case GDK_KEY_KP_0: {
         /* Alt+0 belongs to window 10; only Ctrl+0 resets scaling. */
         if (!control || alt) {
            return FALSE;
         }
         zoom_key_step(100 - (int)zoom_percent, false);

         return TRUE;
      }
      case GDK_KEY_plus:
      case GDK_KEY_equal:
      case GDK_KEY_KP_Add: {
         zoom_key_step(1, control);

         return TRUE;
      }
      case GDK_KEY_minus:
      case GDK_KEY_KP_Subtract: {
         zoom_key_step(-1, control);

         return TRUE;
      }
   }

   return false;
}
