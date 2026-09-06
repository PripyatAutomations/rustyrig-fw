//
// rrclient/ui.c: User interface wrapper (for GTK and TUI)
//
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
#include <rrclient/ui.h>
#include <rrclient/gtk.serverpick.h>

// Default to TUI mode, it will be set to UI_MODE_GTK if $DISPLAY is set
enum GuiMode ui_mode = UI_MODE_TUI;

// Print formatted texted, stdarg version
bool ui_vprint(const char *window, const char *fmt, va_list ap) {
   if (!fmt) {
      return true;
   }

   if (ui_mode == UI_MODE_GTK) {
#if defined(USE_GTK)
      va_list aq;
      va_copy(aq, ap);

      ui_print_gtk(window, fmt, aq);
      va_end(aq);
#endif
   } else if (ui_mode == UI_MODE_TUI) {
      tui_window_t *win = tui_window_find(window);

      if (!win) {
         /* Try to figure out if this is a special window */
      }

      tui_vprint(win, fmt, ap);
   }

   return false;
}

// Print formatted text via ui_vprint()
bool ui_print(const char *window, const char *fmt, ...) {
   if (!fmt) {
      return true;
   }

   va_list ap;
   va_start(ap, fmt);
   bool ret = ui_vprint(window, fmt, ap);
   va_end(ap);

   return ret;
}

void show_server_chooser(void) {
   if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      gui_window_t *old_win = gui_find_window(NULL, "serverpick");

      if (old_win && old_win->gtk_win) {
         Log(LOG_DEBUG, "gtk.serverpick", "show_server_chooser() called while already open");
         gtk_window_present( GTK_WINDOW(old_win->gtk_win) );

         return;
      }
      GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gui_window_t *gui_win = ui_new_window(win, "serverpick");
      GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
      GtkWidget *list = gtk_tree_view_new();
      GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Servers", renderer, "text", 0, NULL);
      gtk_tree_view_append_column(GTK_TREE_VIEW(list), col);
      gtk_tree_view_set_model( GTK_TREE_VIEW(list), GTK_TREE_MODEL(store) );
      g_object_unref(store);

      gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
      GtkTreeSelection *sel = gtk_tree_view_get_selection( GTK_TREE_VIEW(list) );
      gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);

      // fill store with user@server entries, preselect if it matches server_name
      int rank = 0;
      const char *k;
      char *v;
      GtkTreeIter match_iter;
      gboolean have_match = FALSE;
      while ( (rank = dict_enumerate(cfg, rank, &k, &v) ) >= 0) {
         if (!g_str_has_suffix(k, ".server.user") ) {
            continue;
         }
         const char *name_start = strchr(k, ':');

         if (!name_start) {
            continue;
         }
         name_start++;
         char server[32], user[64];
         sscanf(name_start, "%31[^.]", server);
         snprintf(user, sizeof user, "%s@%s", v, server);
         GtkTreeIter iter;
         gtk_list_store_append(store, &iter);
         gtk_list_store_set(store, &iter, 0, user, -1);
         /* -- this is borken fix it: it should preselect the active server if
          * (!have_match && strcmp(server, server_name) == 0) {
          *        match_iter = iter;
          *        have_match = TRUE;
          *     }
          */
      }

      if (have_match) {
         gtk_tree_selection_select_iter(sel, &match_iter);
      }
      // Always CONNECT: this button connects to the selection, it does not
      // toggle or reflect connection status.
      GtkWidget *btn = gtk_button_new_with_label("CONNECT");
      gtk_widget_set_tooltip_text(btn, "Connect to the selected server");
      g_signal_connect(btn, "clicked", G_CALLBACK(on_connect_clicked), list);
      g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);
      g_signal_connect(list, "row-activated", G_CALLBACK(on_row_activated), NULL);  // double-click
                                                                                    // handler

      gtk_box_pack_start(GTK_BOX(vbox), list, TRUE, TRUE, 0);
      gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
      gtk_container_add(GTK_CONTAINER(win), vbox);
      gtk_window_set_default_size(GTK_WINDOW(win), 300, 200);

      gtk_window_set_title(GTK_WINDOW(win), "Server Choser");
      gtk_widget_show_all(win);
      gtk_widget_realize(win);
      place_window(win);
#endif	// USE_GTK
   } else if (ui_mode == UI_MODE_TUI) {
      ui_print(NULL, "| Server picker:");

      // fill list from cfg, matching the gtk.serverpick.c logic
      int rank = 0;
      const char *k;
      char *v;
      while ( (rank = dict_enumerate(cfg, rank, &k, &v) ) >= 0) {
         if (!k) {
            continue;
         }
         size_t klen = strlen(k);

         // match the gtk.serverpick.c check for keys ending in ".server.user"
         if (klen < 12 || strcmp(&k[klen - 12], ".server.user") != 0) {
            continue;
         }
         const char *name_start = strchr(k, ':');

         if (!name_start) {
            continue;
         }
         name_start++;
         char server[32];
         sscanf(name_start, "%31[^.]", server);
         ui_print(NULL, "|    %s - %s", server, v);
      }
      ui_print(NULL, "| Type /server [name] to connect to one of these.");
   }
}
