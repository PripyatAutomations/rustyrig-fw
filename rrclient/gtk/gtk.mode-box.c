//
// rrclient/gtk/gtk.mode-box.c: Modulation mode/width widget
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.chat.h>
#include <rrclient/vfo.h>
extern rrconn_t *ws_conn;
extern time_t now;
extern int cfg_ui_edit_delay;   // main.c
GtkWidget *mode_combo = NULL;
GtkWidget *width_combo = NULL;

static gboolean mode_popup_open = FALSE;
static void on_mode_popup(GtkComboBox *b, gpointer u) {
   mode_popup_open = TRUE;
}
static void on_mode_popdown(GtkComboBox *b, gpointer u) {
   mode_popup_open = FALSE;
}

gulong mode_changed_handler_id;
gulong width_changed_handler_id = 0;

// Timestamp of the last mode command sent by this widget; vfo_update_ui()
// suppresses mode combo updates for ui.edit-delay seconds after a local
// send, since the server's poll echo may still carry the previous mode.
time_t modebox_last_send = 0;

// Timestamp of the last width change we sent ourselves. vfo.c skips UI
// updates from the server's poll echo for cfg_ui_edit_delay seconds after
// send, since the server's poll echo may still carry the previous width.
time_t widthbox_last_send = 0;

static void on_width_changed(GtkComboBoxText *combo, gpointer user_data) {
   const gchar *text = gtk_combo_box_text_get_active_text(combo);

   if (text) {
      // Send width command over websocket: the server accepts both the
      // canned NARR/NORM/WIDE labels and numeric "<hz> Hz" entries.
#if     defined(USE_MONGOOSE)
      char vfo[2] = {
         vfo_state_get_active(), '\0'
      };
      ws_send_width_cmd_in_room(ws_conn, vfo, text, gtk_chat_current_room());
      widthbox_last_send = now;
#endif // defined(USE_MONGOOSE)
      g_free((gchar *) text);
   }
}

static void on_mode_changed(GtkComboBoxText *combo, gpointer user_data) {
   const gchar *text = gtk_combo_box_text_get_active_text(combo);

   if (text) {
      // Send mode command over websocket as before
#if     defined(USE_MONGOOSE)
      char vfo[2] = {
         vfo_state_get_active(), '\0'
      };
      const char *room = gtk_chat_current_room();
      if (ws_send_mode_cmd_in_room(ws_conn, vfo, text, room)) {
         modebox_last_send = now;
      } else {
         Log(LOG_WARN, "gtk.modebox",
            "Unable to send mode %s for VFO %s in room %s",
            text, vfo, room ? room : "(none)");
      }
#endif // defined(USE_MONGOOSE)

      // Show/hide repeater dialog locally based on FM mode
      if (g_str_equal(text, "FM")) {
         /* The controls live in the main overlay; opening them does not
          * create another toplevel or steal focus from the mode selector. */
         fm_dialog_show();
      } else {
         fm_dialog_hide();
      }
      g_free((gchar *) text);
   }
}

// Search the actual combo model so newly advertised modes participate too.
static gboolean on_mode_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   if (!event || (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK))) {
      return FALSE;
   }
   GtkComboBox *combo = GTK_COMBO_BOX(user_data);
   GtkTreeModel *model = gtk_combo_box_get_model(combo);
   guint letter = gdk_keyval_to_lower(event->keyval);
   int count = gtk_tree_model_iter_n_children(model, NULL);
   int active = gtk_combo_box_get_active(combo);

   for (int step = 1 ; step <= count ; step++) {
      int index = (active + step) % count;
      GtkTreeIter iter;
      gchar *text = NULL;
      gtk_tree_model_iter_nth_child(model, &iter, NULL, index);
      gtk_tree_model_get(model, &iter, 0, &text, -1);
      bool match = text && g_ascii_tolower(text[0]) == letter;
      g_free(text);

      if (match) {
         gtk_combo_box_set_active(combo, index);

         return TRUE;
      }
   }

   return FALSE;
}

static gboolean on_width_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   if (!event || (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK))) {
      return FALSE;
   }
   const char *width = NULL;

   switch (gdk_keyval_to_lower(event->keyval)) {
      case GDK_KEY_a: {
         width = "NARR";
         break;
      }
      case GDK_KEY_n: {
         width = "NORM";
         break;
      }
      case GDK_KEY_w: {
         width = "WIDE";
         break;
      }
      default: {
         return FALSE;
      }
   }
   set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(user_data), width);

   return TRUE;
}

static gboolean on_selector_keypress(GtkWidget *widget, GdkEventKey *event, gpointer combo) {
   gboolean handled = combo == width_combo ? on_width_keypress(widget, event, combo) : on_mode_keypress(widget, event, combo);

   if (handled && GTK_IS_MENU(widget) && gtk_widget_get_realized(widget)) {
      GList *items = gtk_container_get_children(GTK_CONTAINER(widget));
      GtkWidget *item = g_list_nth_data(items, gtk_combo_box_get_active(GTK_COMBO_BOX(combo)));

      if (item) {
         gtk_menu_shell_select_item(GTK_MENU_SHELL(widget), item);
      }
      g_list_free(items);
   }

   return handled;
}

static gboolean on_selector_event(GtkWidget *widget, GdkEvent *event, gpointer combo) {
   return event && event->type == GDK_KEY_PRESS ? on_selector_keypress(widget, (GdkEventKey *) event, combo) : FALSE;
}

// Handle the popup's generic event before GTK's own combo key handler consumes it.
static void connect_popup_keys(GtkComboBox *combo) {
   AtkObject *accessible = gtk_combo_box_get_popup_accessible(combo);
   GtkWidget *popup = GTK_IS_ACCESSIBLE(accessible) ? gtk_accessible_get_widget(GTK_ACCESSIBLE(accessible)) : NULL;

   if (popup && !g_object_get_data(G_OBJECT(popup), "rr-selector-keys")) {
      g_signal_connect(popup, "event", G_CALLBACK(on_selector_event), combo);
      g_object_set_data(G_OBJECT(popup), "rr-selector-keys", combo);
   }
}

static void on_selector_popup(GtkComboBox *combo, GParamSpec *pspec, gpointer user) {
   gboolean shown = FALSE;
   g_object_get(combo, "popup-shown", &shown, NULL);

   if (shown) {
      connect_popup_keys(combo);
   }
}

// Server observations must not fire the user-edit handlers and send controls back.
void modebox_update_state(const char *mode, int width) {
   if (mode_combo) {
      gchar *displayed = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(mode_combo));

      /* A recent operator selection is provisional until the bridge's
       * observed mode catches up. Old CAT/poll state must not immediately
       * overwrite AM (or any other requested mode) in the selector. */
      if (mode && !g_ascii_strcasecmp(displayed ? displayed : "", mode)) {
         modebox_last_send = 0;
      }
      bool awaiting_mode = modebox_last_send > 0 &&
         now >= modebox_last_send && now - modebox_last_send < 5;
      if (!awaiting_mode && g_strcmp0(displayed, mode)) {
         g_signal_handler_block(mode_combo, mode_changed_handler_id);

         if (mode) {
            set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), mode);
         } else {
            gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), -1);
         }
         g_signal_handler_unblock(mode_combo, mode_changed_handler_id);
      }
      g_free(displayed);
   }

   if (!width_combo) {
      return;
   }

   if (width <= 0) {
      if (gtk_combo_box_get_active(GTK_COMBO_BOX(width_combo)) != -1) {
         g_signal_handler_block(width_combo, width_changed_handler_id);
         gtk_combo_box_set_active(GTK_COMBO_BOX(width_combo), -1);
         g_signal_handler_unblock(width_combo, width_changed_handler_id);
      }

      return;
   }

   char label[32];
   snprintf(label, sizeof(label), "%d Hz", width);
   GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(width_combo));
   GtkTreeIter row;
   gchar *displayed = NULL;
   bool has_observed_row = gtk_tree_model_iter_nth_child(model, &row, NULL, 3);

   if (has_observed_row) {
      gtk_tree_model_get(model, &row, 0, &displayed, -1);
   }
   bool replace = g_strcmp0(displayed, label) != 0;
   g_free(displayed);

   if (!replace && gtk_combo_box_get_active(GTK_COMBO_BOX(width_combo)) == 3) {
      return;
   }

   g_signal_handler_block(width_combo, width_changed_handler_id);

   // Preserve the exact observed row while unchanged; replacing it on every
   // poll forces GTK to rebuild layout even when the radio is idle.
   if (replace) {
      if (has_observed_row) {
         gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(width_combo), 3);
      }
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), label);
   }
   gtk_combo_box_set_active(GTK_COMBO_BOX(width_combo), 3);
   g_signal_handler_unblock(width_combo, width_changed_handler_id);
}

GtkWidget *create_mode_box(void) {
   GtkWidget *mode_combo_wrapper = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   GtkWidget *mode_box_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(mode_box_label), "Mo<u>d</u>e/<u>W</u>idth");

   ///////
   mode_combo = gtk_combo_box_text_new();
   gtk_widget_set_tooltip_text(mode_combo, "Modulation Mode (type a first letter to cycle matching modes)");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "CW");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "AM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "LSB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "USB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-L");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-U");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "FM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "DFM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "M-L");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "M-U");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "MFM");
   gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 0);

   gtk_widget_set_can_focus(mode_combo_wrapper, TRUE);
   gtk_widget_add_events(mode_combo_wrapper, GDK_KEY_PRESS_MASK);
   gtk_box_pack_start(GTK_BOX(mode_box), mode_box_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(mode_combo_wrapper), mode_combo, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(mode_box), mode_combo_wrapper, FALSE, FALSE, 0);

   ///
   width_combo = gtk_combo_box_text_new();
   gtk_widget_set_tooltip_text(width_combo, "Modulation Width (A: narrow, N: normal, W: wide)");

   // XXX: This should get populated by available khz widths from server for rig
   // too
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "NARR");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "NORM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "WIDE");
   gtk_combo_box_set_active(GTK_COMBO_BOX(width_combo), 1);

   g_signal_connect(width_combo, "key-press-event", G_CALLBACK(on_width_keypress), width_combo);
   g_signal_connect(width_combo, "notify::popup-shown", G_CALLBACK(on_selector_popup), NULL);
   width_changed_handler_id = g_signal_connect(width_combo, "changed", G_CALLBACK(on_width_changed), NULL);
   gtk_box_pack_start(GTK_BOX(mode_box), width_combo, FALSE, FALSE, 0);

   /* Seven character columns for both mode and numeric width (e.g. 2400 Hz). */
   GtkWidget *selectors[] = {
      mode_combo, width_combo
   };

   for (guint i = 0 ; i < G_N_ELEMENTS(selectors) ; ++i) {
      GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(selectors[i]));

      for (GList *cell = cells ; cell ; cell = cell->next) {
         if (GTK_IS_CELL_RENDERER_TEXT(cell->data)) {
            g_object_set(cell->data, "width-chars", 7, NULL);
         }
      }

      g_list_free(cells);
   }

   ///////
   mode_changed_handler_id = g_signal_connect(mode_combo, "changed", G_CALLBACK(on_mode_changed), NULL);
   g_signal_connect(mode_combo_wrapper, "key-press-event", G_CALLBACK(on_mode_keypress), mode_combo);
   g_signal_connect(mode_combo, "key-press-event", G_CALLBACK(on_mode_keypress), mode_combo);
   g_signal_connect(mode_combo, "notify::popup-shown", G_CALLBACK(on_selector_popup), NULL);
   g_signal_connect(mode_combo, "popup", G_CALLBACK(on_mode_popup), NULL);
   g_signal_connect(mode_combo, "popdown", G_CALLBACK(on_mode_popdown), NULL);

   return mode_box;
}
