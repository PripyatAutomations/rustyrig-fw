// GTK main window construction, layout, fullscreen and confirmation dialogs.
// Part of rustyrig-fw; MIT license without mongoose, GPL with mongoose.
#include "gtk.window.h"
#include "gtk.core.h"
#include "gtk.screensaver.h"
#include "gtk.freqentry.h"
#include "gtk.floating-panel.h"
#include "gtk.chat.h"
#include "gtk.tabs.h"
#include "gtk.winmgr.h"
#include "gtk.hotkey.h"
#include <rrclient/gtk/gtk.file-editor.h>
#include <rrclient/gtk/gtk.resources.h>
#include <rrclient/gtk/gtk.audio-routing.h>
#include <rrclient/gtk/gtk.receiver-directory.h>
#include <rrclient/ui.h>
#include <rrclient/gtk/gtk.connect-button.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern GtkWidget *main_window, *main_notebook, *rr_main_toolbar, *status_tab, *log_tab, *host_log_tab;
extern time_t now;
extern GtkWidget *admin_tab, *config_tab, *chat_entry;
extern GtkWidget *init_admin_tab(void);
extern GtkWidget *init_log_tab(void);
extern bool chat_init(void);
extern bool cfg_fullscreen;

/* Width diagnostic: intentionally read-only. GTK's nested viewports can conceal a child wider than the visible client area. */
static void main_width_diagnostic(GtkWidget *content, GtkAllocation *allocation, gpointer unused) {
   (void)unused;
   GtkWidget *toolbar = g_object_get_data(G_OBJECT(content), "rr-width-toolbar");
   GtkWidget *tabs = g_object_get_data(G_OBJECT(content), "rr-width-tabs");

   if (!toolbar || !tabs) {
      return;
   }
   gint toolbar_min = 0, toolbar_nat = 0, tabs_min = 0, tabs_nat = 0;
   gtk_widget_get_preferred_width(toolbar, &toolbar_min, &toolbar_nat);
   gtk_widget_get_preferred_width(tabs, &tabs_min, &tabs_nat);
   gint toolbar_alloc = gtk_widget_get_allocated_width(toolbar);
   gint tabs_alloc = gtk_widget_get_allocated_width(tabs);
   gint available = allocation->width;
   static gint last_available = -1, last_toolbar = -1, last_tabs = -1;

   if (available == last_available && toolbar_alloc == last_toolbar && tabs_alloc == last_tabs) {
      return;
   }
   last_available = available;
   last_toolbar = toolbar_alloc;
   last_tabs = tabs_alloc;

   if (toolbar_min > available + 8 || tabs_min > available + 8 || toolbar_alloc > available + 8 || tabs_alloc > available + 8) {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "content=%d toolbar(min=%d nat=%d alloc=%d) tabs(min=%d nat=%d alloc=%d)", available, toolbar_min, toolbar_nat,
         toolbar_alloc, tabs_min, tabs_nat, tabs_alloc);
   }
}

static int cfg_ui_gtk_main_tabstrip = GTK_POS_BOTTOM; // module-local
extern bool cfg_ui_gtk_vfo_on_top;  // Defined by the rrclient host.
extern bool cfg_fullscreen;

void frontend_gtk_config_refresh(const char *key) {
   (void)key;
   cfg_fullscreen = cfg_get_bool("ui.full-screen", false);
   g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "Effective startup setting ui.full-screen=%s (raw=%s)", cfg_fullscreen ? "true" : "false", cfg_get(
      "ui.full-screen") ? cfg_get("ui.full-screen") : "(absent)");
   cfg_ui_gtk_vfo_on_top = cfg_get_bool("ui.gtk.vfo-on-top", true);
   const char *tabstrip = cfg_get("ui.gtk.main-tabstrip");

   if (tabstrip && strcasecmp(tabstrip, "left") == 0) {
      cfg_ui_gtk_main_tabstrip = GTK_POS_LEFT;
   } else if (tabstrip && strcasecmp(tabstrip, "right") == 0) {
      cfg_ui_gtk_main_tabstrip = GTK_POS_RIGHT;
   } else if (tabstrip && strcasecmp(tabstrip, "top") == 0) {
      cfg_ui_gtk_main_tabstrip = GTK_POS_TOP;
   } else {
      cfg_ui_gtk_main_tabstrip = GTK_POS_BOTTOM;
   }
}

bool frontend_gtk_config_refresh_cb(const char *key) {
   frontend_gtk_config_refresh(key);

   if (main_window) {
      gtk_css_apply_cfg();
      gtk_ui_zoom_recheck();
   }

   return true;
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

         if (str && strcasecmp(str, text) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
            g_free(str);

            return;
         }
         g_free(str);
         index++;
      } while (gtk_tree_model_iter_next(model, &iter));
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
      gtk_button_set_label(GTK_BUTTON(btn), "WAIT");
      gtk_style_context_add_class(ctx, "conn-pending");
      gtk_style_context_remove_class(ctx, "conn-active");
      gtk_style_context_remove_class(ctx, "conn-idle");
   }
}

static gboolean fullscreen_later(gpointer data) {
   (void)data;

   /* cfg_fullscreen is already the requested initial state; toggling here used to unfullscreen the window and flip the setting to false. */
   if (main_window && cfg_fullscreen) {
      gtk_window_fullscreen(GTK_WINDOW(main_window));
      gtk_ui_zoom_recheck();
   }

   return G_SOURCE_REMOVE;
}

/* Report the state the window manager actually applied, not just our fullscreen request. Run once after startup for release diagnostics. */
static gboolean fullscreen_startup_check(gpointer unused) {
   (void)unused;

   if (!main_window || !gtk_widget_get_window(main_window)) {
      return G_SOURCE_REMOVE;
   }
   GdkWindowState state = gdk_window_get_state(gtk_widget_get_window(main_window));
   bool actual = (state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
   g_log("RustyRig-GTK", cfg_fullscreen
      && !actual ? G_LOG_LEVEL_WARNING : G_LOG_LEVEL_MESSAGE, "Startup window state: requested_fullscreen=%s actual_fullscreen=%s maximized=%s", cfg_fullscreen
      ? "yes" : "no", actual ? "yes" : "no", (state & GDK_WINDOW_STATE_MAXIMIZED) ? "yes" : "no");

   return G_SOURCE_REMOVE;
}

static gboolean on_focus_in(GtkWidget *widget, GdkEventFocus *event, gpointer user_data) {
   if (!widget) {
      return FALSE;
   }
   gtk_window_set_urgency_hint(GTK_WINDOW(widget), FALSE);

   return FALSE;
}

// Map plain Y/N (and y/n) keys to the dialog's Yes/No responses
static gboolean on_confirm_dialog_key(GtkWidget *widget, GdkEventKey *ev, gpointer data) {
   if (!widget || !ev) {
      return FALSE;
   }

   switch (ev->keyval) {
      case 'y':
      case 'Y': {
         gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_YES);

         return TRUE;
      }

      case 'n':
      case 'N': {
         gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_NO);

         return TRUE;
      }
   }

   return FALSE;
}

bool ui_confirm_dialog(GtkWindow *parent, const char *message) {
   if (!message) {
      return false;
   }

   GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message);
   /* Keep confirmation prompts in the middle of the display even when the parent is a tab or a partially off-screen window. */
   gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
   /* Quit confirmation must remain accessible even if another utility dialog has an always-on-top hint. Transient/modal alone cannot guarantee that. */
   gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);
   gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
   /* Keep the confirmation above independent always-on-top floating windows. Some WMs do not rank two keep-above windows according to transient order.
    * Temporarily suspend those hints for the lifetime of the modal prompt. */
   GList *top_windows = gtk_window_list_toplevels();
   GList *demoted = NULL;

   for (GList *it = top_windows ; it ; it = it->next) {
      GtkWidget *window = it->data;
      GdkWindow *gdk_window = GTK_IS_WINDOW(window) ? gtk_widget_get_window(window) : NULL;

      if (window != dialog && gdk_window && gtk_widget_get_visible(window) && (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_ABOVE)) {
         gtk_window_set_keep_above(GTK_WINDOW(window), FALSE);
         demoted = g_list_prepend(demoted, g_object_ref(window));
      }
   }

   g_list_free(top_windows);
   g_signal_connect(dialog, "key-press-event", G_CALLBACK(on_confirm_dialog_key), NULL);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
   gtk_widget_show(dialog);
   gtk_window_present(GTK_WINDOW(dialog));
   gboolean confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
   gtk_widget_destroy(dialog);

   for (GList *it = demoted ; it ; it = it->next) {
      GtkWidget *window = it->data;

      if (GTK_IS_WINDOW(window)) {
         gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
      }
      g_object_unref(window);
   }

   g_list_free(demoted);

   return confirmed;
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   (void)widget;
   (void)event;
   (void)data;
   event_emit("client.quit.request", NULL, NULL);

   return TRUE;                 // core handles shutdown after GTK confirmation
}

static GtkWidget *rr_client_overlay;
static void main_toolbar_tool_clicked(GtkButton *button, gpointer data) {
   (void)button;
   const char *tool = data;
   if (!strcmp(tool, "explorer")) event_emit("client.resources.open", NULL, NULL);
   else if (!strcmp(tool, "audio")) gtk_audio_graph_present();
   else {
      GtkWidget *page = !strcmp(tool, "admin") ? admin_tab : config_tab;
      if (page && GTK_IS_NOTEBOOK(main_notebook)) {
         int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), page);
         if (index >= 0)
            gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
         if (!strcmp(tool, "admin")) {
            extern void gtk_admin_refresh(void);
            gtk_admin_refresh();
         }
      }
   }
}
static void main_toolbar_tool(GtkWidget *toolbar, const char *text,
                              const char *icon, const char *tool) {
   GtkWidget *button = gtk_button_new_with_label(text);
   GtkWidget *image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
   gtk_button_set_image(GTK_BUTTON(button), image);
   gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
   gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
   g_signal_connect(button, "clicked", G_CALLBACK(main_toolbar_tool_clicked), (gpointer)tool);
   gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
}

static void main_toolbar_minimize(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;

   if (main_window) {
      gtk_window_iconify(GTK_WINDOW(main_window));
   }
}

static void main_toolbar_quit(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   event_emit("client.quit.request", NULL, NULL);
}


static void frontend_gtk_main_window_destroy(GtkWidget *widget, gpointer unused) {
   (void)unused;

   if (main_window == widget) {
      main_window = NULL;
   }

   if (gtk_main_level() > 0) {
      gtk_main_quit();
   }
}

bool gui_init(void) {
   // Apply user CSS (from [gtk-css] config section) or the default from
   // defconfig.c. See cfg.gtkcss.c, reloadable at runtime with /css-reload
   gtk_css_apply_cfg();

   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gui_window_t *main_window_t = ui_new_window(main_window, "main");
   gtk_window_set_title(GTK_WINDOW(main_window), "rustyrig remote client");

   // Apply the default UI font to the whole window tree. Widgets which set
   // their own font (chat, frequency entry, etc) keep it; everything else
   // (labels, buttons, tabs, ...) inherits this one.
   // Attach the notebook to the main window for tabs
   main_notebook = gtk_notebook_new();
   /* The shared status/command toolbar belongs above every notebook page. */
   GtkWidget *main_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   /* GtkScrolledWindow must never give the complete UI a horizontal
    * minimum derived from a wide notebook page or transient toolbar text. */
   gtk_widget_set_size_request(main_content, 1, -1);
   rr_main_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_widget_set_hexpand(rr_main_toolbar, TRUE);
   gtk_widget_set_margin_start(rr_main_toolbar, 4);
   GtkWidget *minimize = gtk_button_new_from_icon_name("window-minimize-symbolic", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(minimize), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(minimize, "Minimize the main window (VFO locator stays open)");
   g_signal_connect(minimize, "clicked", G_CALLBACK(main_toolbar_minimize), NULL);
   gtk_box_pack_start(GTK_BOX(rr_main_toolbar), minimize, FALSE, FALSE, 0);
   GtkWidget *quit_button = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
   gtk_button_set_relief(GTK_BUTTON(quit_button), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(quit_button, "Quit RustyRig");
   g_signal_connect(quit_button, "clicked", G_CALLBACK(main_toolbar_quit), NULL);
   /* Keep Quit outside the clipped notebook/toolbar viewport. Dynamic
    * room content may exceed its horizontal allocation, but the close control
    * must remain anchored to the visible toplevel's right edge. */
   gtk_widget_set_halign(quit_button, GTK_ALIGN_END);
   gtk_widget_set_valign(quit_button, GTK_ALIGN_START);
   gtk_widget_set_margin_top(quit_button, 2);
   gtk_widget_set_margin_end(quit_button, 2);
   gtk_box_pack_start(GTK_BOX(main_content), rr_main_toolbar, FALSE, TRUE, 1);
   GtkWidget *tabs_wrapper = gtk_wrapping_tabs_new(GTK_NOTEBOOK(main_notebook), cfg_ui_gtk_main_tabstrip);
   gtk_widget_set_size_request(tabs_wrapper, 1, -1);
   gtk_box_pack_start(GTK_BOX(main_content), tabs_wrapper, TRUE, TRUE, 0);

   /* Preferred-width queries inside size-allocate can force nested GTK layout recalculation on every resize/focus transition. Keep this diagnostic opt-in;
    * never run it on the normal GUI hot path. */
   if (g_strcmp0(g_getenv("RRCLIENT_GTK_WIDTH_DIAGNOSTIC"), "1") == 0) {
      g_object_set_data(G_OBJECT(main_content), "rr-width-toolbar", rr_main_toolbar);
      g_object_set_data(G_OBJECT(main_content), "rr-width-tabs", tabs_wrapper);
      g_signal_connect(main_content, "size-allocate", G_CALLBACK(main_width_diagnostic), NULL);
   }
   gtk_ui_zoom_attach(main_window, main_content);
   /* gtk_ui_zoom_attach installs the viewport as the window child. Reparent it into an overlay so the screensaver covers the whole content area. */
   GtkWidget *viewport = gtk_bin_get_child(GTK_BIN(main_window));
   g_object_ref(viewport);
   gtk_container_remove(GTK_CONTAINER(main_window), viewport);
   rr_client_overlay = gtk_overlay_new();
   gtk_widget_set_hexpand(rr_client_overlay, TRUE);
   gtk_widget_set_vexpand(rr_client_overlay, TRUE);
   gtk_container_add(GTK_CONTAINER(main_window), rr_client_overlay);
   gtk_container_add(GTK_CONTAINER(rr_client_overlay), viewport);
   gtk_overlay_add_overlay(GTK_OVERLAY(rr_client_overlay), quit_button);
   gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(rr_client_overlay), quit_button, FALSE);
   GtkWidget *tab_strip = g_object_get_data(G_OBJECT(main_notebook), "rr-tab-strip-widget");
   GtkWidget *upper_extra = cfg_ui_gtk_main_tabstrip == GTK_POS_TOP ? tab_strip : NULL;
   GtkWidget *lower = cfg_ui_gtk_main_tabstrip == GTK_POS_BOTTOM ? tab_strip : NULL;
   rr_floating_panel_set_reserved_widgets(GTK_OVERLAY(rr_client_overlay), rr_main_toolbar, upper_extra, lower, RR_FLOATING_PANEL_SCROLLBAR_INSET);
   g_object_unref(viewport);
   g_signal_connect(main_window, "key-press-event", G_CALLBACK(gtk_screensaver_key_press), NULL);
   g_signal_connect(main_window, "event", G_CALLBACK(gtk_screensaver_activity_event), NULL);
   gtk_screensaver_activity();
   g_timeout_add_seconds(1, gtk_screensaver_idle_check, NULL);
   gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_notebook), FALSE);
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook), cfg_ui_gtk_main_tabstrip);

   /* Tool pages occupy the main notebook but never appear in the tab strip.
    * Toolbar actions (and named focus commands) select them directly. */
   admin_tab = init_admin_tab();
   config_tab = init_config_tab();
   if (admin_tab) {
      g_object_set_data(G_OBJECT(admin_tab), "rr-tab-hidden", GINT_TO_POINTER(1));
      GtkWidget *label = gtk_label_new("Admin");
      gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), admin_tab, label);
      gtk_widget_set_no_show_all(label, TRUE);
      gtk_widget_hide(label);
   }
   if (config_tab) {
      g_object_set_data(G_OBJECT(config_tab), "rr-tab-hidden", GINT_TO_POINTER(1));
      GtkWidget *label = gtk_label_new("Config");
      gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), config_tab, label);
      gtk_widget_set_no_show_all(label, TRUE);
      gtk_widget_hide(label);
   }
   // One GTK Log tab combines Client and Host streams; TUI tabs stay separate.
   log_tab = init_log_tab();
   host_log_tab = log_tab;

   //// CHAT stuff (alt-5+)...
   chat_init();

   /* Tool pages are selected from the toolbar, not the tab strip. */
   main_toolbar_tool(rr_main_toolbar, "Explorer", "system-search-symbolic", "explorer");
   main_toolbar_tool(rr_main_toolbar, "Audio Graph", "multimedia-volume-control-symbolic", "audio");
   main_toolbar_tool(rr_main_toolbar, "Admin", "preferences-system-symbolic", "admin");
   main_toolbar_tool(rr_main_toolbar, "Config", "preferences-other-symbolic", "config");

   // Shared serial console page, with top-aligned endpoint subtabs.
   extern GtkWidget *init_terminal_tab(void);
   init_terminal_tab();

   // GTK Signals
   g_signal_connect(main_window, "window-state-event", G_CALLBACK(on_window_state), NULL);
   g_signal_connect(main_window, "destroy", G_CALLBACK(frontend_gtk_main_window_destroy), NULL);
   g_signal_connect(main_window, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
   g_signal_connect(main_window, "delete-event", G_CALLBACK(on_window_delete), NULL);

   // bind our hotkeys
   gui_hotkey_register(main_window);

   // Video (webcam) frame listener
   extern void rrclient_webcam_register(void);  // gtk.webcam.c
   rrclient_webcam_register();

   /* Ask GTK for fullscreen before the window is mapped. An idle callback after show_all() can lose the race against the initial WM placement. */
   if (cfg_fullscreen) {
      g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "Startup requests fullscreen (ui.full-screen=true)");
      gtk_window_fullscreen(GTK_WINDOW(main_window));
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "Startup requests normal window (ui.full-screen=false)");
   }

   // Make the main window on screen
   gtk_widget_show_all(main_window);
   gtk_widget_realize(main_window);
   place_window(main_window);

   /* Never restore an oversized normal window beyond its monitor workarea. The zoom layer can then shrink the 100% layout to the available size. */
   if (!cfg_fullscreen) {
      GdkWindow *gdk_window = gtk_widget_get_window(main_window);
      GdkDisplay *display = gtk_widget_get_display(main_window);
      GdkMonitor *monitor = gdk_window ? gdk_display_get_monitor_at_window(display, gdk_window) : gdk_display_get_primary_monitor(display);

      if (monitor) {
         GdkRectangle work;
         gdk_monitor_get_workarea(monitor, &work);
         int width = 0, height = 0;
         gtk_window_get_size(GTK_WINDOW(main_window), &width, &height);

         if (width > work.width || height > work.height) {
            gtk_window_resize(GTK_WINDOW(main_window), MIN(width, work.width), MIN(height, work.height));
         }
      }
      gtk_ui_zoom_recheck();
   }
   // Fonts are handled entirely by the [gtk-css] section (see cfg.gtkcss.c)
   int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), status_tab);

   if (index != -1) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
   }

   /* Window placement runs after mapping; reassert once after it finishes. The helper is idempotent and does not toggle the configured flag. */
   if (cfg_fullscreen) {
      g_idle_add(fullscreen_later, NULL);
   }
   g_timeout_add(1000, fullscreen_startup_check, NULL);
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));

   ui_print(NULL, "%srustyrig client started", get_chat_ts(now));

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

   for (GtkWidget *w = focused ; w ; w = gtk_widget_get_parent(w)) {
      if (w == ancestor) {
         return TRUE;
      }
   }

   return FALSE;
}

bool gui_fullscreen_toggle(void) {
   if (cfg_fullscreen) {
      gtk_window_unfullscreen(GTK_WINDOW(main_window));
      gtk_window_set_decorated(GTK_WINDOW(main_window), TRUE);
   } else {
      gtk_window_fullscreen(GTK_WINDOW(main_window));
      gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
   }
   cfg_fullscreen = !cfg_fullscreen;

   return false;
}

