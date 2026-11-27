// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRCLIENT_GTK_FLOATING_PANEL_H
#define RRCLIENT_GTK_FLOATING_PANEL_H
#include <gtk/gtk.h>

typedef struct rr_floating_panel RRFloatPanel;
#define RR_FLOATING_PANEL_SCROLLBAR_INSET 32
/* The caller owns the returned panel and must destroy it before the overlay. Its body is a normal GTK box into which arbitrary controls can be packed. */
RRFloatPanel *rr_floating_panel_new(GtkOverlay *overlay, GtkNotebook *tabs, const char *id, const char *title);
void rr_floating_panel_set_reserved_widgets(GtkOverlay *overlay, GtkWidget *upper, GtkWidget *upper_extra, GtkWidget *lower, gint right_inset);
GtkWidget *rr_floating_panel_body(RRFloatPanel *panel);
GtkWidget *rr_floating_panel_widget(RRFloatPanel *panel);
void rr_floating_panel_show(RRFloatPanel *panel);
/* Explicit user-requested opening can reveal one panel even while the global temporary hide mask is active (e.g. Server Picker on Status). It does not reveal
 * any other panel or change the hide toggle. */
void rr_floating_panel_present(RRFloatPanel *panel);
/* Center the panel in the main overlay on its first show, ignoring saved positions. */
void rr_floating_panel_set_initial_centered(RRFloatPanel *panel, gboolean centered);
/* Preferred location for a new panel only; persisted user geometry wins. */
void rr_floating_panel_set_initial_position(RRFloatPanel *panel, gint x, gint y);
void rr_floating_panel_set_title(RRFloatPanel *panel, const char *title);
void rr_floating_panel_set_title_click(RRFloatPanel *panel, GCallback callback, gpointer data);
void rr_floating_panel_hide(RRFloatPanel *panel);
void rr_floating_panel_restore(RRFloatPanel *panel);
void rr_floating_panel_set_closable(RRFloatPanel *panel, gboolean closable);
void rr_floating_panel_set_minimizable(RRFloatPanel *panel, gboolean minimizable);
/* Utility windows remain in the floating overlay above the workspace. */
void rr_floating_panel_set_floating_only(RRFloatPanel *panel);
/* Rehome script panels inside a workspace overlay (not the global overlay). */
gboolean rr_floating_panel_root_overlay(RRFloatPanel *panel, GtkOverlay *overlay);
void rr_floating_panel_set_resizable(RRFloatPanel *panel, gboolean resizable);
/* Follow the controls' natural size; zoom only, never manual border resizing. */
void rr_floating_panel_set_content_sized(RRFloatPanel *panel);
/* Explicit outer-panel size after shrinkwrap, in pixels. */
void rr_floating_panel_set_dimensions(RRFloatPanel *panel, gint width, gint height);
/* Hide the floating chrome and optionally make a widget the drag handle. */
void rr_floating_panel_set_compact(RRFloatPanel *panel, GtkWidget *drag_handle);
/* Optional content-driven sizing; the minimum tracks this widget at panel zoom. */
void rr_floating_panel_set_min_width_widget(RRFloatPanel *panel, GtkWidget *widget);
void rr_floating_panel_fit_content(RRFloatPanel *panel);
/* Discard legacy oversized saved geometry for panels with redesigned compact layouts; retains their screen position and other preferences. */
void rr_floating_panel_reset_saved_size(RRFloatPanel *panel);
/* Zoom adjustment -100..+200 relative to 100% (0 = normal size). Custom content (e.g. Cairo graphs) handles its own rendering at this scale. */
void rr_floating_panel_set_zoom_callback(RRFloatPanel *panel, void (*callback) (double scale, gpointer user), gpointer user);
void rr_floating_panel_set_pinned(RRFloatPanel *panel, gboolean pinned);
void rr_floating_panel_destroy(RRFloatPanel *panel);
void rr_floating_panel_set_close_callback(RRFloatPanel *panel, void (*callback) (gpointer), gpointer data);
/* Modal panels stay inside the client's overlay. Response handlers may destroy the panel; they are called synchronously on a button press or Escape. */
void rr_floating_panel_set_modal(RRFloatPanel *panel, gboolean modal);
void rr_floating_panel_set_response_callback(RRFloatPanel *panel, void (*callback) (RRFloatPanel *, gint, gpointer), gpointer data);
void rr_floating_panel_response(RRFloatPanel *panel, gint response);
GtkWidget *rr_floating_panel_add_response(RRFloatPanel *panel, GtkWidget *button_box, const char *label, gint response);
gboolean rr_floating_panel_is_minimized(const RRFloatPanel *panel);
/* Transient, GTK-thread-only visibility masks. These never persist panel state, change minimization, or restore panels which were already hidden. */
#define RR_FLOATING_HIDE_MANUAL (1u << 0)
#define RR_FLOATING_HIDE_TAB (1u << 1)
/* Wrap the central content in four independently splittable edge docks. */
GtkWidget *rr_floating_panel_chat_workspace(GtkWidget *chat, GtkWidget *page);
GtkWidget *rr_floating_panel_main_workspace(GtkWidget *content, GtkWidget *notebook);
/* Update room-bound visibility while retaining the shared workspace. */
void rr_floating_panel_chat_activate(GtkWidget *page);
/* Room-bound panels follow only the matching chat tab; NULL means global. */
void rr_floating_panel_set_chat_room(RRFloatPanel *panel, const char *server, const char *room);
/* Use a dock edge only if no user choice has been saved for this panel. */
void rr_floating_panel_default_dock(RRFloatPanel *panel, guint edge);
/* Client-owned scriptable floating-panel API. Returned strings are g_free'd
 * by caller. Regex matches titles; location is N/E/W/S/M or *.
 * A returned ID is a stable name, not a borrowed GTK pointer. */
gboolean rr_floating_panel_find_match(const char *pattern, const char *location,
                                      char **id, char **title, char **where);
gboolean rr_floating_panel_move_named(const char *id, const char *location, gint x, gint y);
gboolean rr_floating_panel_zoom_named(const char *id, gint percent);
gboolean rr_floating_panel_opacity_named(const char *id, gint percent);
void rr_floating_panels_set_hidden(guint reason, gboolean hidden);
gboolean rr_floating_panels_hidden(guint reason);

#endif
