//
// rrclient/gtk.core.c: Core of GTK gui
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// XXX: Need to break this into pieces and wrap up our custom widgets, soo we can do
// XXX: nice things like pop-out (floating) VFOs
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
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/gtk.freqentry.h"

extern dict *cfg;
GtkWidget *main_window = NULL;
GtkWidget *conn_button = NULL;
GtkWidget *freq_entry = NULL;
GtkWidget *toggle_userlist_button = NULL;
GtkWidget *main_notebook = NULL;
GtkWidget *main_tab = NULL;
GtkWidget *log_tab = NULL;
GtkCssProvider *css_provider = NULL;
bool cfg_use_gtk = true;	// Default to using GTK3

extern GtkWidget *init_log_tab(void);
extern GtkWidget *init_admin_tab(void);
extern GtkWidget *config_tab;
extern GtkWidget *admin_tab;
extern bool chat_init(void);		// gtk.chat.c

bool ui_print_gtk(const char *msg) {
   if (!msg) {
      return true;
   }

   // Append our text to the end of the GtkTextView
   GtkTextIter end;
   gtk_text_buffer_get_end_iter(text_buffer, &end);
   gtk_text_buffer_insert(text_buffer, &end, msg, -1);
   gtk_text_buffer_insert(text_buffer, &end, "\n", 1);

   // Scroll after the current main loop iteration, this ensures widget is fully drawn and scroll will be complete
   g_idle_add(ui_scroll_to_end, chat_textview);
   return false;
}

void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text) {
   if (!combo || !text) {
      return;
   }

   GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
   GtkTreeIter iter;
   int index = 0;

   if (gtk_tree_model_get_iter_first(model, &iter)) {
      do {
         gchar *str = NULL;
         gtk_tree_model_get(model, &iter, 0, &str, -1);

         if (str && strcmp(str, text) == 0) {
            g_signal_handler_block(combo, mode_changed_handler_id);
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
            g_signal_handler_unblock(combo, mode_changed_handler_id);
            g_free(str);
            return;
         }
         g_free(str);
         index++;
      } while (gtk_tree_model_iter_next(model, &iter));
   }
}

void update_connection_button(bool connected, GtkWidget *btn) {
   if (!btn) {
      return;
   }

   GtkStyleContext *ctx = gtk_widget_get_style_context(btn);

   if (connected) {
      gtk_button_set_label(GTK_BUTTON(btn), "Online");
      gtk_style_context_remove_class(ctx, "conn-idle");
      gtk_style_context_add_class(ctx, "conn-active");
   } else {
      gtk_button_set_label(GTK_BUTTON(btn), "Offline");
      gtk_style_context_remove_class(ctx, "conn-active");
      gtk_style_context_add_class(ctx, "conn-idle");
   }
}

static gboolean on_focus_in(GtkWidget *widget, GdkEventFocus *event, gpointer user_data) {
   if (!widget) {
      return FALSE;
   }

   gtk_window_set_urgency_hint(GTK_WINDOW(widget), FALSE);
   return FALSE;
}

bool gui_init(void) {
   css_provider = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css_provider,
      ".ptt-active { background: red; color: white; }"
      ".ptt-idle { background: #0fc00f; color: white; }"
      ".conn-active { background: #0fc00f; color: white; }"
      ".conn-idle { background: red; color: white; }",
      -1, NULL);

   gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(css_provider),
      GTK_STYLE_PROVIDER_PRIORITY_USER);

   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gui_window_t *main_window_t = ui_new_window(main_window, "main");
   gtk_window_set_title(GTK_WINDOW(main_window), "rustyrig remote client");

   // Attach the notebook to the main window for tabs
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(main_window), main_notebook);
   main_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *main_tab_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(main_tab_label), "(<u>1</u>) Control");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), main_tab, main_tab_label);

   ///////////
   // VFO A //
   ///////////
   GtkWidget *vfo_a = create_vfo_box();
   // If docked, we wont create a window, but it might be created later similarly
   bool vfo_docked = cfg_get_bool("vfo-a.docked", true);
   if (vfo_docked) {
     // Attach to the main window
      gtk_box_pack_start(GTK_BOX(main_tab), vfo_a, FALSE, FALSE, 0);
   } else {
     // Floating
     gui_window_t *vfo_win = create_vfo_window(vfo_a, 'A');
     gtk_container_add(GTK_CONTAINER(vfo_win->gtk_win), vfo_a);
   }

   // MAIN tab (alt-1)
   //// CHAT stuff...
   chat_init();
   GtkWidget *chat_box = create_chat_box();
   gtk_box_pack_start(GTK_BOX(main_tab), chat_box, TRUE, TRUE, 0);


   // ADMIN tab (alt-2)
   admin_tab = init_admin_tab();

   // CONFIG tab (alt-3)
   config_tab = init_config_tab();
   // LOG tab (alt-4)
   log_tab = init_log_tab();

   // Signals
   g_signal_connect(main_window, "window-state-event", G_CALLBACK(on_window_state), NULL);
   g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
   g_signal_connect(main_window, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
//   g_signal_connect(main_window, "key-press-event", G_CALLBACK(handle_global_hotkey), main_notebook);
   gui_hotkey_register(main_window);

   // Generate and display a userlist for this server 
   userlist_init();

   // Make the main window on screen
   gtk_widget_show_all(main_window);
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
   gtk_widget_realize(main_window);
   place_window(main_window);

   ui_print("[%s] rustyrig client started", get_chat_ts());

   return false;
}

gboolean is_widget_or_descendant_focused(GtkWidget *ancestor) {
   if (!ancestor) {
      return FALSE;
   }

   GtkWidget *toplevel = gtk_widget_get_toplevel(ancestor);
   if (!GTK_IS_WINDOW(toplevel)) {
      return FALSE;
   }

   GtkWidget *focused = gtk_window_get_focus(GTK_WINDOW(toplevel));
   for (GtkWidget *w = focused; w; w = gtk_widget_get_parent(w)) {
      if (w == ancestor) {
         return TRUE;
      }
   }
   return FALSE;
}
