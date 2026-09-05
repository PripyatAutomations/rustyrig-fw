//
// rrclient/gtk.core.c: Core of GTK gui
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
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>
#include <rrclient/gtk.core.h>
#include <rrclient/gtk.freqentry.h>
#include <rrclient/ui.colors.h>

#define	MSGBUF_SIZE 8096

extern dict *cfg;
extern time_t now;
GtkWidget *main_window = NULL;
GtkWidget *conn_button = NULL;
GtkWidget *freq_entry = NULL;
GtkWidget *toggle_userlist_button = NULL;
GtkWidget *main_notebook = NULL;
GtkWidget *status_tab = NULL;
GtkWidget *log_tab = NULL;
GtkCssProvider *css_provider = NULL;
bool cfg_use_gtk = true;         // Default to using GTK3
extern GtkWidget *init_log_tab(void);
extern int cfg_ui_gtk_main_tabstrip;	// main.c
extern GtkWidget *init_admin_tab(void);
extern bool cfg_ui_gtk_vfo_on_top;
extern bool chat_init(void);             // gtk.chat.c
bool cfg_fullscreen = false;

char *gtk_colorize_string(const char *in) {
   if (!in) {
      return NULL;
   }
   size_t len = strlen(in);
   char *out = malloc(len * 8 + 64);

   if (!out) {
      return NULL;
   }
   char *o = out;
   bool bold = false, italic = false, underline = false;
   const char *fg = NULL, *bg = NULL;

   const char *p = in;
   while (*p) {
      if (*p == '{') {
         const char *end = strchr(p, '}');

         if (!end) {
            *o++ = *p++;
            continue;
         }
         size_t key_len = (size_t)(end - (p + 1) );
         char key[64];

         if (key_len >= sizeof(key) ) {
            key_len = sizeof(key) - 1;
         }
         memcpy(key, p + 1, key_len);
         key[key_len] = '\0';

         if (strcmp(key, "reset") == 0) {
            if (fg || bg) {
               o += sprintf(o, "</span>");
               fg = bg = NULL;
            }

            if (bold) {
               o += sprintf(o, "</b>"); bold = false;
            }

            if (italic) {
               o += sprintf(o, "</i>"); italic = false;
            }

            if (underline) {
               o += sprintf(o, "</u>"); underline = false;
            }
         } else if (strcmp(key, "bold") == 0) {
            if (!bold) {
               o += sprintf(o, "<b>"); bold = true;
            }
         } else if (strcmp(key, "italic") == 0) {
            if (!italic) {
               o += sprintf(o, "<i>"); italic = true;
            }
         } else if (strcmp(key, "underline") == 0) {
            if (!underline) {
               o += sprintf(o, "<u>"); underline = true;
            }
         } else if (strcmp(key, "bold-off") == 0) {
            if (bold) {
               o += sprintf(o, "</b>"); bold = false;
            }
         } else if (strcmp(key, "italic-off") == 0) {
            if (italic) {
               o += sprintf(o, "</i>"); italic = false;
            }
         } else if (strcmp(key, "underline-off") == 0) {
            if (underline) {
               o += sprintf(o, "</u>"); underline = false;
            }
         } else {
            bool is_bg = false;
            const char *pango_color = pango_color_for_tag(key, &is_bg);

            if (pango_color) {
               if (fg || bg) {
                  o += sprintf(o, "</span>");
                  fg = bg = NULL;
               }
               o += sprintf(o, "<span");

               if (!is_bg) {
                  o += sprintf(o, " foreground=\"%s\"", pango_color);
                  fg = pango_color;
               } else {
                  o += sprintf(o, " background=\"%s\"", pango_color);
                  bg = pango_color;
               }
               o += sprintf(o, ">");
            }
         }
         p = end + 1;
      } else {
         const char *next = strchr(p, '{');
         size_t chunk_len = next ? (size_t)(next - p) : strlen(p);

         char *escaped = g_markup_escape_text(p, (gint)chunk_len);
         o += sprintf(o, "%s", escaped);
         g_free(escaped);

         p += chunk_len;
      }
   }

   if (fg || bg) {
      o += sprintf(o, "</span>");
   }

   if (bold) {
      o += sprintf(o, "</b>");
   }

   if (italic) {
      o += sprintf(o, "</i>");
   }

   if (underline) {
      o += sprintf(o, "</u>");
   }
   *o = '\0';

   return out;
}

bool ui_print_gtk(const char *window, const char *fmt, va_list ap) {
   if (!fmt) {
      return true;
   }

   char msgbuf[MSGBUF_SIZE];
   memset( msgbuf, 0, sizeof(msgbuf) );

   va_list aq;
   va_copy(aq, ap);
   vsnprintf(msgbuf, sizeof(msgbuf), fmt, aq);
   va_end(aq);

   bool colorize_failed = false;
   char *colorized = gtk_colorize_string(msgbuf);

   if (!colorized) {
      colorize_failed = true;
      colorized = msgbuf;
      Log(LOG_WARN, "ui.gtk3", "ui_print_gtk: gtk_colorize_string failed");
   }

   GtkTextIter end;

   gtk_text_buffer_get_end_iter(text_buffer, &end);
   gtk_text_buffer_insert_markup(text_buffer, &end, colorized, -1);
   gtk_text_buffer_insert(text_buffer, &end, "\n", 1);

   if (!colorize_failed) {
      g_free(colorized);
   }

   g_idle_add(ui_scroll_to_end, chat_textview);

   return false;
}

void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text) {
   if (!combo || !text) {
      return;
   }
   GtkTreeModel *model = gtk_combo_box_get_model( GTK_COMBO_BOX(combo) );
   GtkTreeIter iter;
   int index = 0;

   if (gtk_tree_model_get_iter_first(model, &iter) ) {
      do{
         gchar *str = NULL;
         gtk_tree_model_get(model, &iter, 0, &str, -1);

         if (str && strcasecmp(str, text) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
            g_free(str);

            return;
         }
         g_free(str);
         index++;
      } while (gtk_tree_model_iter_next(model, &iter) );
   }
}

void update_connection_button(int connected, GtkWidget *btn) {
   if (!btn) {
      return;
   }
   GtkStyleContext *ctx = gtk_widget_get_style_context(btn);

   if (!ctx) {
      return;
   }

   if (connected == 1) {
      gtk_button_set_label(GTK_BUTTON(btn), "Online");
      gtk_style_context_remove_class(ctx, "conn-idle");
      gtk_style_context_remove_class(ctx, "conn-pending");
      gtk_style_context_add_class(ctx, "conn-active");
   } else if (connected == 0) {
      gtk_button_set_label(GTK_BUTTON(btn), "Offline");
      gtk_style_context_add_class(ctx, "conn-idle");
      gtk_style_context_remove_class(ctx, "conn-active");
      gtk_style_context_remove_class(ctx, "conn-pending");
   } else if (connected == -1) {
      gtk_button_set_label(GTK_BUTTON(btn), "Trying...");
      gtk_style_context_add_class(ctx, "conn-pending");
      gtk_style_context_remove_class(ctx, "conn-active");
      gtk_style_context_remove_class(ctx, "conn-idle");
   }
}

static gboolean fullscreen_later(gpointer data) {
   gui_fullscreen_toggle();

   return G_SOURCE_REMOVE;
}

static gboolean on_focus_in(GtkWidget *widget, GdkEventFocus *event, gpointer user_data) {
   if (!widget) {
      return FALSE;
   }
   gtk_window_set_urgency_hint(GTK_WINDOW(widget), FALSE);

   return FALSE;
}

// This pops up and confirms the user if they want to quit. True return should exit
bool ui_confirm_quit(void) {
   if (ui_mode == UI_MODE_GTK) {
      GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
         GTK_BUTTONS_YES_NO, "Confirm quit?");

      gboolean cancel = gtk_dialog_run( GTK_DIALOG(dialog) ) != GTK_RESPONSE_YES;

      gtk_widget_destroy(dialog);

      if (cancel) {
         return false;
      }
   } else if (ui_mode == UI_MODE_TUI) {
      ui_print( NULL, "Confirm quit? (Y/N) - NYI, Quit %s:%d", __FILE__, __LINE__);
      // XXX: Set input mode to accept this and return false to cancel
   }

   // Confirm the quit
   dying = true;

   return true;
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   if ( ui_confirm_quit() ) {
      dying = true;

      return FALSE;  // allow GTK to destroy the window
   }

   return TRUE;      // cancel close
}

bool gui_init(void) {
   gui_font_init();
   css_provider = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css_provider, ".ptt-active { background: red; color: white; }"
      ".ptt-idle { background: #0fc00f; color: white; }"
      ".ptt-pending { background: yellow; color: black; }"
      ".ptt-offline { background: #555555; color: white; }"
      ".ptt-tot { background: orange; color: black; }"
      ".conn-active { background: #0fc00f; color: white; }"
      ".conn-pending { background: yellow; color: black; }"
      ".conn-idle { background: red; color: white; }", -1, NULL);

   gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider),
      GTK_STYLE_PROVIDER_PRIORITY_USER);

   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gui_window_t *main_window_t = ui_new_window(main_window, "main");
   gtk_window_set_title(GTK_WINDOW(main_window), "rustyrig remote client");

   // Attach the notebook to the main window for tabs
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(main_window), main_notebook);
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook), cfg_ui_gtk_main_tabstrip);

   // ADMIN tab (alt-1)
   admin_tab = init_admin_tab();
   // CONFIG tab (alt-2)
   config_tab = init_config_tab();
   // LOG tab (alt-3)
   log_tab = init_log_tab();

   //// CHAT stuff (alt-4+)...
   chat_init();

   // GTK Signals
   g_signal_connect(main_window, "window-state-event", G_CALLBACK(on_window_state), NULL);
   g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
   g_signal_connect(main_window, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
   g_signal_connect(main_window, "delete-event", G_CALLBACK(on_window_delete), NULL);

   // bind our hotkeys
   gui_hotkey_register(main_window);

   // Make the main window on screen
   gtk_widget_show_all(main_window);
   gtk_widget_realize(main_window);
   place_window(main_window);

   if (ui_mode == UI_MODE_GTK) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), status_tab);

      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
   } else if (ui_mode == UI_MODE_TUI) {
   }

   // enforce fullscreen if set
   if (cfg_fullscreen) {
      Log(LOG_INFO, "ui.gtk3", "Going fullscreen since cfg:ui.full-screen is set!");

      g_idle_add(fullscreen_later, NULL);
   }
   gtk_widget_grab_focus( GTK_WIDGET(chat_entry) );

   ui_print( NULL, "%s rustyrig client started", get_chat_ts(now) );
   return false;
}

gboolean is_widget_or_descendant_focused(GtkWidget *ancestor) {
   if (!ancestor) {
      return FALSE;
   }
   GtkWidget *toplevel = gtk_widget_get_toplevel(ancestor);

   if (!GTK_IS_WINDOW(toplevel) ) {
      return FALSE;
   }
   GtkWidget *focused = gtk_window_get_focus( GTK_WINDOW(toplevel) );

   for (GtkWidget *w = focused ; w ; w = gtk_widget_get_parent(w) ) {
      if (w == ancestor) {
         return TRUE;
      }
   }

   return FALSE;
}

bool fullscreen = false;

bool gui_fullscreen_toggle(void) {
   if (fullscreen) {
      gtk_window_unfullscreen( GTK_WINDOW(main_window) );
      gtk_window_set_decorated(GTK_WINDOW(main_window), TRUE);
   } else {
      gtk_window_fullscreen( GTK_WINDOW(main_window) );
      gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
   }
   fullscreen = !fullscreen;

   return false;
}
