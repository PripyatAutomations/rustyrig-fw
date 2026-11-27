// GTK main window construction, layout, fullscreen and confirmation dialogs.
// Part of rustyrig-fw.
// SPDX-License-Identifier: GPL-2.0-only
#include "gtk.window.h"
#include "gtk.core.h"
#include "gtk.screensaver.h"
#include "gtk.freqentry.h"
#include "gtk.floating-panel.h"
#include "gtk.scripts.h"
#include "gtk.chat.h"
#include "gtk.tabs.h"
#include "gtk.winmgr.h"
#include "gtk.hotkey.h"
#include <rrclient/gtk/gtk.file-editor.h>
#include <rrclient/gtk/gtk.resources.h>
#include <rrclient/gtk/gtk.audio-routing.h>
#include <rrclient/gtk/gtk.serial.h>
#include <rrclient/sercom.h>
#include <rrclient/database.h>
#include <rrclient/connman.h>
#include <librustyaxe/event-bus.h>
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

/* Labels are separate widgets so the icon and hit target remain intact when the toolbar is compacted. The scroll viewport provides the usable width. */
#define RR_TOOLBAR_ACTIONS 10
static GtkWidget *toolbar_action_labels[RR_TOOLBAR_ACTIONS];
static unsigned toolbar_action_count;
static bool toolbar_titles_visible;
static GtkWidget *toolbar_action_viewport;
static GtkWidget *panels_toggle;

static void toolbar_titles_update(int available) {
   if (!GTK_IS_WIDGET(rr_main_toolbar) || available <= 0) {
      return;
   }
   bool show = !cfg_get_bool("ui.status.collapse-buttons", true);

   if (show) {
      int minimum = 0, natural = 0;
      gtk_widget_get_preferred_width(rr_main_toolbar, &minimum, &natural);

      /* In compact mode, calculate the extra width of all labels; while expanded, the preferred width already includes them. */
      if (!toolbar_titles_visible) {
         for (unsigned i = 0 ; i < toolbar_action_count ; ++i) {
            GtkWidget *label = toolbar_action_labels[i];

            if (!label) {
               continue;
            }
            PangoLayout *layout = gtk_widget_create_pango_layout(label, gtk_label_get_text(GTK_LABEL(label)));
            int width = 0;
            pango_layout_get_pixel_size(layout, &width, NULL);
            g_object_unref(layout);
            natural += width + 6;
         }
      }
      show = available >= natural + 8;
   }

   if (show == toolbar_titles_visible) {
      return;
   }
   toolbar_titles_visible = show;

   for (unsigned i = 0 ; i < toolbar_action_count ; ++i) {
      GtkWidget *label = toolbar_action_labels[i];

      if (label) {
         if (show) {
            gtk_widget_show(label);
         } else {
            gtk_widget_hide(label);
         }
      }
   }
}

static void toolbar_viewport_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer unused) {
   (void)widget;
   (void)unused;
   toolbar_titles_update(allocation->width);
}

static int cfg_ui_gtk_main_tabstrip = GTK_POS_BOTTOM; // module-local
extern bool cfg_ui_gtk_vfo_on_top;  // Defined by the rrclient host.
extern bool cfg_fullscreen;

void frontend_gtk_config_refresh(const char *key) {
   (void)key;
   cfg_fullscreen = cfg_get_bool("ui.full-screen", false);

   if (toolbar_action_viewport) {
      toolbar_titles_update(gtk_widget_get_allocated_width(toolbar_action_viewport));
   }
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
/* Keep this independent of panel close/minimize state: manual hiding survives leaving Status/Log until the operator toggles it back. */
static void main_toolbar_panels_toggled(GtkToggleButton *button, gpointer unused) {
   (void)unused;
   gboolean hide = gtk_toggle_button_get_active(button);
   rr_floating_panels_set_hidden(RR_FLOATING_HIDE_MANUAL, hide);
   gtk_widget_set_tooltip_text(GTK_WIDGET(button), hide ? "Show previously visible floating panels" : "Temporarily hide floating panels");
}

static void main_panels_page_switched(GtkNotebook *book, GtkWidget *page, guint number, gpointer unused) {
   (void)book;
   (void)number;
   (void)unused;
   /* Only explicitly designated utility pages maximize the main region.
    * All other pages keep the shared auxiliary workspace. */
   gboolean zoom = page && (page == status_tab || page == log_tab ||
      g_object_get_data(G_OBJECT(page), "rr-main-zoom"));
   rr_floating_panels_set_hidden(RR_FLOATING_HIDE_TAB, zoom);
   rr_floating_panel_chat_activate(page);
}

/* Virtual Rigs manager. The stable per-slot name is independent of the selected chat; all serial changes cross to the core event domain. */
static RRFloatPanel *vrig_panel;
static GtkWidget *vrig_status;
static GtkWidget *vrig_profile[RR_SERCOM_VRIG_MAX], *vrig_radio[RR_SERCOM_VRIG_MAX];
static rr_event_token_t vrig_result_token;
static void vrig_bound(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!vrig_status || !message || !message->payload || message->length != sizeof(rr_sercom_vrig_bind_result_t)) {
      return;
   }
   const rr_sercom_vrig_bind_result_t *result = message->payload;
   gtk_label_set_text(GTK_LABEL(vrig_status), result->detail);
}

static void vrig_bind_clicked(GtkButton *button, gpointer unused) {
   (void)unused;
   unsigned slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "rr-vrig-slot"));

   if (slot >= RR_SERCOM_VRIG_MAX || !vrig_profile[slot] || !vrig_radio[slot]) {
      return;
   }
   rr_sercom_vrig_bind_t req = {
      .slot = slot
   };
   g_strlcpy(req.profile, gtk_entry_get_text(GTK_ENTRY(vrig_profile[slot])), sizeof(req.profile));
   g_strlcpy(req.rig, gtk_entry_get_text(GTK_ENTRY(vrig_radio[slot])), sizeof(req.rig));
   rr_event_message_t msg = {
      .name = RR_SERCOM_VRIG_BIND_REQUEST,
      .payload = &req, .length = sizeof(req)
   };
   event_emit_message(&msg);
}

static void vrig_unbind_clicked(GtkButton *button, gpointer unused) {
   (void)unused;
   unsigned slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "rr-vrig-slot"));

   if (slot >= RR_SERCOM_VRIG_MAX) {
      return;
   }
   rr_sercom_vrig_bind_t req = {
      .slot = slot
   };
   rr_event_message_t msg = {
      .name = RR_SERCOM_VRIG_BIND_REQUEST,
      .payload = &req, .length = sizeof(req)
   };
   event_emit_message(&msg);
}

static void vrig_configure_clicked(GtkButton *button, gpointer unused) {
   (void)unused;
   const char *name = g_object_get_data(G_OBJECT(button), "rr-vrig-port");

   if (name) {
      gtk_serial_configure(GTK_WINDOW(main_window), name);
   }
}

static void vrig_open_routing(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   gtk_audio_graph_present();
}

/* Create a small, theme-native endpoint chip. The serial editor is still the one authoritative configuration UI for a physical/PTY endpoint. */
static GtkWidget *vrig_device_button(const char *name, const char *icon) {
   GtkWidget *button = gtk_button_new();
   GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *symbol = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
   GtkWidget *label = gtk_label_new(name);
   gtk_box_pack_start(GTK_BOX(content), symbol, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
   gtk_container_add(GTK_CONTAINER(button), content);
   gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
   /* Keep the entire device chip a finger-sized hit target. */
   gtk_widget_set_size_request(button, 112, 44);
   gtk_widget_set_margin_top(button, 2);
   gtk_widget_set_margin_bottom(button, 2);
   g_object_set_data_full(G_OBJECT(button), "rr-vrig-port", g_strdup(name), g_free);
   g_signal_connect(button, "clicked", G_CALLBACK(vrig_configure_clicked), NULL);
   char *path = g_build_filename(g_get_home_dir(), "dev", name, NULL);
   char *tip = g_strdup_printf("%s — configure serial endpoint", path);
   gtk_widget_set_tooltip_text(button, tip);
   g_free(tip);
   g_free(path);

   return button;
}

static GtkWidget *vrig_field(const char *caption, GtkWidget *entry) {
   GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *label = gtk_label_new(caption);
   gtk_label_set_xalign(GTK_LABEL(label), 0.);
   gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim-label");
   gtk_box_pack_start(GTK_BOX(column), label, FALSE, FALSE, 0);
   gtk_widget_set_size_request(entry, -1, 44);
   gtk_box_pack_start(GTK_BOX(column), entry, FALSE, FALSE, 0);

   return column;
}

static void vrig_manager_open(void) {
   if (!GTK_IS_OVERLAY(rr_client_overlay) || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }

   if (vrig_panel) {
      rr_floating_panel_present(vrig_panel);

      return;
   }
   vrig_panel = rr_floating_panel_new(GTK_OVERLAY(rr_client_overlay), GTK_NOTEBOOK(main_notebook), "virtual-rigs", "Virtual Rigs");

   if (!vrig_panel) {
      return;
   }
   rr_floating_panel_set_initial_position(vrig_panel, 90, 105);
   GtkWidget *body = rr_floating_panel_body(vrig_panel);
   gtk_container_set_border_width(GTK_CONTAINER(body), 10);

   GtkWidget *intro = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *heading = gtk_label_new("One client, multiple radios");
   gtk_label_set_xalign(GTK_LABEL(heading), 0.);
   gtk_style_context_add_class(gtk_widget_get_style_context(heading), "title");
   GtkWidget *hint = gtk_label_new("Each virtual rig binds numbered CAT and GPS ports to one remote radio, " "independent of the currently selected tab.");
   gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
   gtk_label_set_xalign(GTK_LABEL(hint), 0.);
   gtk_label_set_max_width_chars(GTK_LABEL(hint), 60);
   gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
   gtk_box_pack_start(GTK_BOX(intro), heading, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(intro), hint, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(body), intro, FALSE, FALSE, 5);

   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), FALSE);
   gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroll), TRUE);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 1);
   gtk_widget_set_size_request(scroll, 610, 350);
   GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
   gtk_container_set_border_width(GTK_CONTAINER(list), 3);
   gtk_container_add(GTK_CONTAINER(scroll), list);
   gtk_box_pack_start(GTK_BOX(body), scroll, TRUE, TRUE, 2);
   const char *current = rrclient_selected_server();

   for (unsigned slot = 0 ; slot < RR_SERCOM_VRIG_MAX ; slot++) {
      char name[48], cat[32], gps[32];
      g_snprintf(name, sizeof(name), "vrig%u", slot);
      g_snprintf(cat, sizeof(cat), "ttyCAT%u", slot);
      g_snprintf(gps, sizeof(gps), "ttyGPS%u", slot);
      GtkWidget *frame = gtk_frame_new(NULL);
      gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
      GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
      gtk_container_set_border_width(GTK_CONTAINER(card), 12);
      gtk_container_add(GTK_CONTAINER(frame), card);

      GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      GtkWidget *rig_label = gtk_label_new(NULL);
      char *title = g_strdup_printf("<b>%s</b>", name);
      gtk_label_set_markup(GTK_LABEL(rig_label), title);
      g_free(title);
      gtk_label_set_xalign(GTK_LABEL(rig_label), 0.);
      gtk_box_pack_start(GTK_BOX(header), rig_label, TRUE, TRUE, 0);
      GtkWidget *ports = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_box_pack_start(GTK_BOX(ports), vrig_device_button(cat, "utilities-terminal-symbolic"), FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(ports), vrig_device_button(gps, "mark-location-symbolic"), FALSE, FALSE, 0);
      gtk_box_pack_end(GTK_BOX(header), ports, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(card), header, FALSE, FALSE, 0);

      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      vrig_profile[slot] = gtk_entry_new();
      vrig_radio[slot] = gtk_entry_new();
      char *key = g_strdup_printf("serial.vrig.%u.profile", slot);
      char *saved_profile = rrclient_database_setting_get(key);
      g_free(key);
      key = g_strdup_printf("serial.vrig.%u.rig", slot);
      char *saved_rig = rrclient_database_setting_get(key);
      g_free(key);
      gtk_entry_set_text(GTK_ENTRY(vrig_profile[slot]), saved_profile && *saved_profile ? saved_profile : current ? current : "");

      if (saved_rig && *saved_rig) {
         gtk_entry_set_text(GTK_ENTRY(vrig_radio[slot]), saved_rig);
      } else {
         g_snprintf(name, sizeof(name), "rig%u", slot);
         gtk_entry_set_text(GTK_ENTRY(vrig_radio[slot]), name);
      }
      g_free(saved_profile);
      g_free(saved_rig);
      gtk_entry_set_width_chars(GTK_ENTRY(vrig_profile[slot]), 18);
      gtk_entry_set_width_chars(GTK_ENTRY(vrig_radio[slot]), 9);
      gtk_entry_set_placeholder_text(GTK_ENTRY(vrig_profile[slot]), "Server profile");
      gtk_box_pack_start(GTK_BOX(row), vrig_field("Server", vrig_profile[slot]), TRUE, TRUE, 0);
      gtk_box_pack_start(GTK_BOX(row), vrig_field("Remote rig", vrig_radio[slot]), FALSE, FALSE, 0);
      GtkWidget *bind = gtk_button_new_with_label("Bind");
      gtk_widget_set_size_request(bind, 80, 44);
      gtk_style_context_add_class(gtk_widget_get_style_context(bind), "suggested-action");
      gtk_widget_set_valign(bind, GTK_ALIGN_END);
      gtk_widget_set_tooltip_text(bind, "Create or update pinned serial endpoints");
      g_object_set_data(G_OBJECT(bind), "rr-vrig-slot", GUINT_TO_POINTER(slot));
      g_signal_connect(bind, "clicked", G_CALLBACK(vrig_bind_clicked), NULL);
      gtk_box_pack_end(GTK_BOX(row), bind, FALSE, FALSE, 0);
      GtkWidget *unbind = gtk_button_new_from_icon_name("edit-clear-symbolic", GTK_ICON_SIZE_MENU);
      gtk_widget_set_size_request(unbind, 44, 44);
      gtk_widget_set_valign(unbind, GTK_ALIGN_END);
      gtk_widget_set_tooltip_text(unbind, "Disconnect CAT/GPS ports for this virtual rig");
      g_object_set_data(G_OBJECT(unbind), "rr-vrig-slot", GUINT_TO_POINTER(slot));
      g_signal_connect(unbind, "clicked", G_CALLBACK(vrig_unbind_clicked), NULL);
      gtk_box_pack_end(GTK_BOX(row), unbind, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(card), row, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(list), frame, FALSE, FALSE, 0);
   }

   GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   vrig_status = gtk_label_new("Select a server and radio, then Bind.");
   gtk_label_set_line_wrap(GTK_LABEL(vrig_status), TRUE);
   gtk_label_set_xalign(GTK_LABEL(vrig_status), 0.);
   gtk_widget_set_hexpand(vrig_status, TRUE);
   gtk_style_context_add_class(gtk_widget_get_style_context(vrig_status), "dim-label");
   gtk_box_pack_start(GTK_BOX(footer), vrig_status, TRUE, TRUE, 0);
   GtkWidget *routing = gtk_button_new_with_label("Open Routing");
   gtk_widget_set_size_request(routing, 130, 44);
   g_signal_connect(routing, "clicked", G_CALLBACK(vrig_open_routing), NULL);
   gtk_box_pack_end(GTK_BOX(footer), routing, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(body), footer, FALSE, FALSE, 4);

   if (!vrig_result_token) {
      vrig_result_token = event_on_message_domain_token(RR_SERCOM_VRIG_BIND_RESULT, vrig_bound, NULL, "gtk");
   }
   gtk_widget_show_all(body);
   rr_floating_panel_present(vrig_panel);
}

static void main_toolbar_tool_clicked(GtkButton *button, gpointer data) {
   (void)button;
   const char *tool = data;

   if (!strcmp(tool, "explorer")) {
      event_emit("client.resources.open", NULL, NULL);
   } else if (!strcmp(tool, "sdr-directory")) {
      /* Same GTK event-domain action as Alt-Z. */
      event_emit("client.receiver-directory.show", NULL, NULL);
   } else if (!strcmp(tool, "recordings")) {
      event_emit("client.recordings.open", NULL, NULL);
   } else if (!strcmp(tool, "scripts")) {
      gtk_scripts_open();
   } else if (!strcmp(tool, "audio")) {
      gtk_audio_graph_toggle();
   } else if (!strcmp(tool, "virtual-rigs")) {
      vrig_manager_open();
   } else {
      GtkWidget *page = !strcmp(tool, "admin") ? admin_tab : config_tab;

      if (page && GTK_IS_NOTEBOOK(main_notebook)) {
         int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), page);

         if (index >= 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
         }

         if (!strcmp(tool, "admin")) {
            extern void gtk_admin_refresh(void);
            gtk_admin_refresh();
         }
      }
   }
}

static void main_toolbar_tool(GtkWidget *toolbar, const char *text, const char *icon, const char *tool, const char *tooltip) {
   GtkWidget *button = gtk_button_new();
   GtkWidget *contents = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
   GtkWidget *image = !strcmp(icon, "📻") ? gtk_label_new(icon) : gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
   GtkWidget *label = gtk_label_new(text);
   gtk_box_pack_start(GTK_BOX(contents), image, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(contents), label, FALSE, FALSE, 0);
   gtk_container_add(GTK_CONTAINER(button), contents);
   /* no-show-all preserves compact layout during the initial show_all(). */
   gtk_widget_set_no_show_all(label, TRUE);
   gtk_widget_hide(label);

   if (toolbar_action_count < RR_TOOLBAR_ACTIONS) {
      toolbar_action_labels[toolbar_action_count++] = label;
   }
   /* The toolbar sits inside a clipped viewport. GTK's tooltip hit testing can resolve the innermost image/label instead of the enclosing button. Give every
    * visible hover target the same tooltip. */
   gtk_widget_set_tooltip_text(button, tooltip);
   gtk_widget_set_tooltip_text(contents, tooltip);
   gtk_widget_set_tooltip_text(image, tooltip);
   gtk_widget_set_tooltip_text(label, tooltip);
   gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
   /* GTK themes may render a 16px icon, but keep an easy finger target. */
   gtk_widget_set_size_request(button, 30, 28);
   g_signal_connect(button, "clicked", G_CALLBACK(main_toolbar_tool_clicked), (gpointer) tool);
   gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
}

/* The main window uses its own small chrome. Delegate drags to the WM so xfwm4 edge tiling and keyboard window operations keep working. */
static gboolean main_toolbar_drag_press(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget;
   (void)unused;

   if (!GTK_IS_WINDOW(main_window) || event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }

   if (event->type == GDK_2BUTTON_PRESS) {
      GdkWindow *gdk = gtk_widget_get_window(main_window);

      if (gdk && (gdk_window_get_state(gdk) & GDK_WINDOW_STATE_MAXIMIZED)) {
         gtk_window_unmaximize(GTK_WINDOW(main_window));
      } else {
         gtk_window_maximize(GTK_WINDOW(main_window));
      }

      return TRUE;
   }

   if (event->type == GDK_BUTTON_PRESS) {
      gtk_window_begin_move_drag(GTK_WINDOW(main_window), event->button, (gint) event->x_root, (gint) event->y_root, event->time);

      return TRUE;
   }

   return FALSE;
}

/* Client-side resize edges for the undecorated main window. All dragging is handed to the WM (xfwm4), so tiling, limits and screen-edge behavior stay native.
 * Each edge has its own small input-only-looking GtkEventBox. */
static GtkWidget *main_resize_handles[8];
static const GdkWindowEdge main_resize_edges[8] = {
   GDK_WINDOW_EDGE_NORTH, GDK_WINDOW_EDGE_SOUTH,
   GDK_WINDOW_EDGE_WEST, GDK_WINDOW_EDGE_EAST,
   GDK_WINDOW_EDGE_NORTH_WEST, GDK_WINDOW_EDGE_NORTH_EAST,
   GDK_WINDOW_EDGE_SOUTH_WEST, GDK_WINDOW_EDGE_SOUTH_EAST
};

static const char *main_resize_cursors[8] = {
   "n-resize", "s-resize", "w-resize", "e-resize",
   "nw-resize", "ne-resize", "sw-resize", "se-resize"
};
static gboolean main_window_resize_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   guint index = GPOINTER_TO_UINT(data) - 1;

   if (index >= G_N_ELEMENTS(main_resize_edges) || !GTK_IS_WINDOW(main_window) ||
      event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY || !gtk_window_get_resizable(GTK_WINDOW(main_window))) {
      return FALSE;
   }
   GdkWindow *gdk = gtk_widget_get_window(main_window);

   if (!gdk || (gdk_window_get_state(gdk) & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN))) {
      return FALSE;
   }
   gtk_window_begin_resize_drag(GTK_WINDOW(main_window), main_resize_edges[index], event->button, (gint) event->x_root, (gint) event->y_root, event->time);

   return TRUE;
}

static gboolean main_window_resize_hover(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
   guint index = GPOINTER_TO_UINT(data) - 1;

   if (index >= G_N_ELEMENTS(main_resize_edges)) {
      return FALSE;
   }
   gboolean enter = event->type == GDK_ENTER_NOTIFY;
   gtk_widget_set_opacity(widget, enter ? .75 : .18);
   GdkWindow *window = gtk_widget_get_window(widget);

   if (window) {
      GdkDisplay *display = gdk_window_get_display(window);
      GdkCursor *cursor = enter ? gdk_cursor_new_from_name(display, main_resize_cursors[index]) : NULL;
      gdk_window_set_cursor(window, cursor);

      if (cursor) {
         g_object_unref(cursor);
      }
   }

   return FALSE;
}

static gboolean main_window_resize_state(GtkWidget *widget, GdkEventWindowState *event, gpointer unused) {
   (void)widget;
   (void)unused;
   gboolean shown = !(event->new_window_state & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN));

   for (guint i = 0 ; i < G_N_ELEMENTS(main_resize_handles) ; i++) {
      if (main_resize_handles[i]) {
         gtk_widget_set_visible(main_resize_handles[i], shown);
      }
   }

   return FALSE;
}

static void main_window_resize_handles_add(GtkOverlay *overlay) {
   static const GtkAlign horizontal[8] = {
      GTK_ALIGN_FILL, GTK_ALIGN_FILL, GTK_ALIGN_START, GTK_ALIGN_END,
      GTK_ALIGN_START, GTK_ALIGN_END, GTK_ALIGN_START, GTK_ALIGN_END
   };
   static const GtkAlign vertical[8] = {
      GTK_ALIGN_START, GTK_ALIGN_END, GTK_ALIGN_FILL, GTK_ALIGN_FILL,
      GTK_ALIGN_START, GTK_ALIGN_START, GTK_ALIGN_END, GTK_ALIGN_END
   };
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css, ".rr-main-resize-edge { background-color: rgba(35,195,230,.12); }"
      ".rr-main-resize-edge:hover { background-color: rgba(35,195,230,.45); }", -1, NULL);
   GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(overlay));

   if (screen) {
      gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   }
   g_object_unref(css);

   for (guint i = 0 ; i < G_N_ELEMENTS(main_resize_handles) ; i++) {
      GtkWidget *handle = gtk_event_box_new();
      gtk_event_box_set_visible_window(GTK_EVENT_BOX(handle), TRUE);
      gtk_widget_set_halign(handle, horizontal[i]);
      gtk_widget_set_valign(handle, vertical[i]);
      gtk_widget_set_size_request(handle, (i < 2) ? -1 : (i < 4) ? 4 : 10, (i < 2) ? 4 : (i < 4) ? -1 : 10);
      gtk_widget_set_opacity(handle, .18);
      gtk_style_context_add_class(gtk_widget_get_style_context(handle), "rr-main-resize-edge");
      gtk_widget_add_events(handle, GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
      g_signal_connect(handle, "button-press-event", G_CALLBACK(main_window_resize_press), GUINT_TO_POINTER(i + 1));
      g_signal_connect(handle, "enter-notify-event", G_CALLBACK(main_window_resize_hover), GUINT_TO_POINTER(i + 1));
      g_signal_connect(handle, "leave-notify-event", G_CALLBACK(main_window_resize_hover), GUINT_TO_POINTER(i + 1));
      gtk_widget_set_tooltip_text(handle, "Drag to resize window");
      gtk_overlay_add_overlay(overlay, handle);
      gtk_widget_show(handle);
      main_resize_handles[i] = handle;
   }

   g_signal_connect(main_window, "window-state-event", G_CALLBACK(main_window_resize_state), NULL);
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
   if (vrig_result_token) {
      event_off_token(vrig_result_token);
      vrig_result_token = 0;
   }

   if (vrig_panel) {
      rr_floating_panel_destroy(vrig_panel);
      vrig_panel = NULL;
   }
   vrig_status = NULL;
   memset(vrig_profile, 0, sizeof(vrig_profile));
   memset(vrig_radio, 0, sizeof(vrig_radio));
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

   /* Reinitialization must never retain label pointers from a destroyed GUI. */
   toolbar_action_count = 0;
   memset(toolbar_action_labels, 0, sizeof(toolbar_action_labels));
   toolbar_titles_visible = false;
   toolbar_action_viewport = NULL;
   panels_toggle = NULL;
   /* Rebuilt UIs must not inherit a stale previous window's manual mask. */
   rr_floating_panels_set_hidden(RR_FLOATING_HIDE_MANUAL, FALSE);
   rr_floating_panels_set_hidden(RR_FLOATING_HIDE_TAB, FALSE);
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gui_window_t *main_window_t = ui_new_window(main_window, "main");
   gtk_window_set_title(GTK_WINDOW(main_window), "rustyrig remote client");
   /* Give xfwm4 a standard top-level, with normal borders and WM tiling. It must remain resizable even with the routing editor selected. */
   gtk_window_set_type_hint(GTK_WINDOW(main_window), GDK_WINDOW_TYPE_HINT_NORMAL);
   gtk_window_set_skip_taskbar_hint(GTK_WINDOW(main_window), FALSE);
   gtk_window_set_skip_pager_hint(GTK_WINDOW(main_window), FALSE);
   gtk_window_set_resizable(GTK_WINDOW(main_window), TRUE);
   gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);

   // Apply the default UI font to the whole window tree. Widgets which set
   // their own font (chat, frequency entry, etc) keep it; everything else
   // (labels, buttons, tabs, ...) inherits this one.
   // Attach the notebook to the main window for tabs
   main_notebook = rr_adaptive_notebook_new();
   /* The shared status/command toolbar belongs above every notebook page. */
   GtkWidget *main_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   /* GtkScrolledWindow must never give the complete UI a horizontal minimum derived from a wide notebook page or transient toolbar text. */
   gtk_widget_set_size_request(main_content, 1, -1);
   rr_main_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
   gtk_widget_set_hexpand(rr_main_toolbar, TRUE);
   gtk_widget_set_margin_start(rr_main_toolbar, 4);
   GtkWidget *minimize = gtk_button_new_from_icon_name("window-minimize-symbolic", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(minimize), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(minimize, "Minimize the main window (VFO locator stays open)");
   GtkWidget *minimize_icon = gtk_button_get_image(GTK_BUTTON(minimize));

   if (minimize_icon) {
      gtk_widget_set_tooltip_text(minimize_icon, "Minimize the main window (VFO locator stays open)");
   }
   g_signal_connect(minimize, "clicked", G_CALLBACK(main_toolbar_minimize), NULL);
   /* The quit control is packed last into the fixed right-hand cluster. */
   GtkWidget *quit_button = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
   gtk_button_set_relief(GTK_BUTTON(quit_button), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(quit_button, "Quit RustyRig");
   GtkWidget *quit_icon = gtk_button_get_image(GTK_BUTTON(quit_button));

   if (quit_icon) {
      gtk_widget_set_tooltip_text(quit_icon, "Quit RustyRig");
   }
   g_signal_connect(quit_button, "clicked", G_CALLBACK(main_toolbar_quit), NULL);
   /* Keep Quit outside the clipped notebook/toolbar viewport. Dynamic room content may exceed its horizontal allocation, but the close control must remain
    * anchored to the visible toplevel's right edge. */
   gtk_widget_set_valign(quit_button, GTK_ALIGN_CENTER);
   gtk_widget_set_margin_end(quit_button, 2);
   /* Fixed right status cluster never shares the oversized minimum width of the left toolbar or the notebook. Give each side its own allocation. */
   GtkWidget *toolbar_shell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   /* A compact app-owned row: icons remain usable, without 42px button minimums or theme padding doubling the available vertical space. */
   GtkCssProvider *toolbar_css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(toolbar_css, ".rr-main-toolbar button { min-height: 26px; min-width: 26px;"
      " padding: 0 3px; margin: 0; border-width: 0; }" ".rr-main-toolbar entry { min-height: 24px; padding: 0 3px; }", -1, NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(toolbar_shell), "rr-main-toolbar");
   /* A provider on only the shell's style context does not cascade all selector rules to its descendants. Register at screen scope. */
   GdkScreen *toolbar_screen = gtk_widget_get_screen(main_window);

   if (toolbar_screen) {
      gtk_style_context_add_provider_for_screen(toolbar_screen, GTK_STYLE_PROVIDER(toolbar_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   }
   g_object_unref(toolbar_css);
   /* Small dedicated drag target: other toolbar controls remain clickable. */
   GtkWidget *move_handle = gtk_event_box_new();
   GtkWidget *move_label = gtk_label_new("⠿");
   gtk_container_add(GTK_CONTAINER(move_handle), move_label);
   gtk_widget_set_size_request(move_handle, 28, 28);
   gtk_widget_set_tooltip_text(move_handle, "Drag to move or tile with XFCE; double-click to maximize/restore");
   gtk_widget_add_events(move_handle, GDK_BUTTON_PRESS_MASK);
   g_signal_connect(move_handle, "button-press-event", G_CALLBACK(main_toolbar_drag_press), NULL);
   gtk_box_pack_start(GTK_BOX(toolbar_shell), move_handle, FALSE, FALSE, 0);
   GtkWidget *toolbar_viewport = gtk_scrolled_window_new(NULL, NULL);
   /* With NEVER, the toolbar's full child requisition can set the WM's minimum window width despite min-content-width=1. */
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(toolbar_viewport), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
   gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(toolbar_viewport), TRUE);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(toolbar_viewport), 1);
   gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(toolbar_viewport), FALSE);
   gtk_widget_set_hexpand(toolbar_viewport, TRUE);
   gtk_container_add(GTK_CONTAINER(toolbar_viewport), rr_main_toolbar);
   toolbar_action_viewport = toolbar_viewport;
   g_signal_connect(toolbar_viewport, "size-allocate", G_CALLBACK(toolbar_viewport_size_allocate), NULL);
   gtk_box_pack_start(GTK_BOX(toolbar_shell), toolbar_viewport, TRUE, TRUE, 0);
   GtkWidget *toolbar_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
   gtk_widget_set_valign(toolbar_right, GTK_ALIGN_CENTER);
   gtk_box_pack_end(GTK_BOX(toolbar_shell), toolbar_right, FALSE, FALSE, 0);
   g_object_set_data(G_OBJECT(main_window), "rr-main-toolbar-right", toolbar_right);
   g_object_set_data(G_OBJECT(main_window), "rr-main-quit-button", quit_button);
   /* Attach the toolbar outside the scrolling content below. */
   GtkWidget *tabs_wrapper = gtk_wrapping_tabs_new(GTK_NOTEBOOK(main_notebook), cfg_ui_gtk_main_tabstrip);
   gtk_widget_set_size_request(tabs_wrapper, 1, -1);
   GtkWidget *workspace = rr_floating_panel_main_workspace(tabs_wrapper, main_notebook);
   gtk_box_pack_start(GTK_BOX(main_content), workspace, TRUE, TRUE, 0);

   gtk_ui_zoom_attach(main_window, main_content);
   /* gtk_ui_zoom_attach installs the viewport as the window child. Reparent it into an overlay so the screensaver covers the whole content area. */
   GtkWidget *viewport = gtk_bin_get_child(GTK_BIN(main_window));
   g_object_ref(viewport);
   gtk_container_remove(GTK_CONTAINER(main_window), viewport);
   rr_client_overlay = gtk_overlay_new();
   gtk_widget_set_hexpand(rr_client_overlay, TRUE);
   gtk_widget_set_vexpand(rr_client_overlay, TRUE);
   gtk_container_add(GTK_CONTAINER(main_window), rr_client_overlay);
   /* GtkOverlay has special placement semantics when its main child is a GtkScrolledWindow: overlays are positioned relative to the scrolled contents*, not the
    * visible viewport. On connected rooms the content may be wider than the window, pushing floating panels and the clock/X offscreen. Make an ordinary box the
    * overlay's main child instead. */
   GtkWidget *viewport_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   gtk_widget_set_hexpand(viewport_host, TRUE);
   gtk_widget_set_vexpand(viewport_host, TRUE);
   gtk_container_add(GTK_CONTAINER(rr_client_overlay), viewport_host);
   main_window_resize_handles_add(GTK_OVERLAY(rr_client_overlay));
   /* The entire toolbar must be outside the app-wide scroll viewport. Otherwise the content can exceed the viewport and shift even the fixed right cluster off
    * screen when a rig room is opened. */
   gtk_box_pack_start(GTK_BOX(viewport_host), toolbar_shell, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(viewport_host), viewport, TRUE, TRUE, 0);
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

   /* Tool pages occupy the main notebook but never appear in the tab strip. Toolbar actions (and named focus commands) select them directly. */
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
   main_toolbar_tool(rr_main_toolbar, "Explorer", "system-search-symbolic", "explorer", "Explorer: Explore available resources in the cluster");
   main_toolbar_tool(rr_main_toolbar, "Remote SDRs", "network-wireless-symbolic", "sdr-directory", "Remote SDRs: Access remote SDRs such as KiwiSDR");
   main_toolbar_tool(rr_main_toolbar, "Recordings", "media-record-symbolic", "recordings", "Record and manage client audio recordings");
   main_toolbar_tool(rr_main_toolbar, "Scripts", "text-x-script", "scripts", "Manage RustyScript files, loading and editing");
   main_toolbar_tool(rr_main_toolbar, "Routing", "audio-card-symbolic", "audio", "Routing: Audio, modem RX/TX, port trims and DSP pipelines");
   main_toolbar_tool(rr_main_toolbar, "Virtual Rigs", "network-wired-symbolic", "virtual-rigs",
      "Virtual Rigs: Independently pin ttyCAT#/ttyGPS# to a remote rig");
   /* Toggle, not a window-manager hide action: it preserves panel placement and minimized tabs. The pressed state indicates manual suppression. */
   panels_toggle = gtk_toggle_button_new();
   GtkWidget *panels_label = gtk_label_new("Panels");
   GtkWidget *panels_icon = gtk_image_new_from_icon_name("view-grid-symbolic", GTK_ICON_SIZE_MENU);
   GtkWidget *panels_contents = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   gtk_box_pack_start(GTK_BOX(panels_contents), panels_icon, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(panels_contents), panels_label, FALSE, FALSE, 0);
   gtk_container_add(GTK_CONTAINER(panels_toggle), panels_contents);
   gtk_button_set_relief(GTK_BUTTON(panels_toggle), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(panels_toggle, "Temporarily hide floating panels");
   gtk_widget_set_tooltip_text(panels_icon, "Temporarily hide floating panels");
   gtk_widget_set_no_show_all(panels_label, TRUE);
   gtk_widget_hide(panels_label);
   g_signal_connect(panels_toggle, "toggled", G_CALLBACK(main_toolbar_panels_toggled), NULL);
   /* Fixed window controls, from left to right: minimize the main window, temporarily hide floating panels, and quit. Keep all three outside the
    * clipped/scrollable toolbar so the X stays reachable. */
   gtk_box_pack_start(GTK_BOX(toolbar_right), minimize, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar_right), panels_toggle, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar_right), quit_button, FALSE, FALSE, 0);
   main_toolbar_tool(rr_main_toolbar, "Admin", "preferences-other-symbolic", "admin", "Admin: Administer the server/cluster");
   main_toolbar_tool(rr_main_toolbar, "Config", "preferences-system-symbolic", "config", "Config: Edit client configuration");

   // Shared serial console page, with top-aligned endpoint subtabs.
   extern GtkWidget *init_terminal_tab(void);
   init_terminal_tab();

   /* Attach after pages exist. This also handles Status/Log tab selection via mouse, keyboard, commands and programmatic navigation. */
   g_signal_connect(main_notebook, "switch-page", G_CALLBACK(main_panels_page_switched), NULL);

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
      gtk_window_unfullscreen(GTK_WINDOW(main_window));
      g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "Startup requests normal, resizable window (ui.full-screen=false)");
   }

   // Make the main window on screen
   gtk_widget_show_all(main_window);
   main_panels_page_switched(GTK_NOTEBOOK(main_notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(
      main_notebook))), (guint) gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook)), NULL);
   toolbar_titles_update(gtk_widget_get_allocated_width(toolbar_action_viewport));
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
   if (!GTK_IS_WINDOW(main_window)) {
      return false;
   }
   GdkWindow *gdk = gtk_widget_get_window(main_window);
   gboolean actual_fullscreen = gdk && (gdk_window_get_state(gdk) & GDK_WINDOW_STATE_FULLSCREEN);

   if (actual_fullscreen || cfg_fullscreen) {
      gtk_window_unfullscreen(GTK_WINDOW(main_window));
      gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
      gtk_window_set_resizable(GTK_WINDOW(main_window), TRUE);
      cfg_fullscreen = false;
   } else {
      /* Fullscreen suppresses the decorations itself. Do not disable them explicitly, or they can remain missing after returning to normal. */
      gtk_window_fullscreen(GTK_WINDOW(main_window));
      cfg_fullscreen = true;
   }
   gtk_ui_zoom_recheck();

   return false;
}
