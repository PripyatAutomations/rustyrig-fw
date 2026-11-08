// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Rig controls and property/menu editing dialogs; GTK owner thread only. */
#include <rrclient/connection.state.h>
#include "gtk.catalog.internal.h"
#include "gtk.control-panel.h"
#include "gtk.serial.h"
#include "gtk.core.h"
#include <rrclient/objects.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/media.h>
#include <rrclient/rooms.h>
#include <rrclient/irc.stats.h>
#include <rrclient/userlist.h>
#include <rrclient/sercom.h>
#include <librustyaxe/transport.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Rig controls are opened on demand, not rendered as live tree sliders. */
static char *resource_property_key(const char *profile, const char *uuid, const char *property) {
   return g_strdup_printf("%s\037%s\037%s", profile ? profile : "", uuid ? uuid : "", property ? property : "");
}
void resource_property_result(const char *event, const char *payload, rrconn_t *unused_connection, void *unused) {
   (void)event;
   (void)unused_connection;
   (void)unused;

   if (!browser || !payload) {
      return;
   }
   dict *snapshot = json2dict(payload);

   if (!snapshot) {
      return;
   }
   const char *request_id = dict_get(snapshot, "request.id", "");
   char *key = resource_property_key(dict_get(snapshot, "profile", ""), dict_get(snapshot, "uuid", ""), dict_get(snapshot, "property", ""));

   if (*request_id) {
      g_hash_table_replace(browser->property_snapshots, key, snapshot);
   } else {
      g_free(key);
      dict_free(snapshot);
   }
}
const dict *resource_property_cached(const char *profile, const char *uuid, const char *property, const char *request_id) {
   if (!browser) {
      return NULL;
   }
   char *key = resource_property_key(profile, uuid, property);
   const dict *snapshot = g_hash_table_lookup(browser->property_snapshots, key);
   g_free(key);

   return snapshot && !g_strcmp0(dict_get((dict *) snapshot, "request.id", ""), request_id) ? snapshot : NULL;
}
char *resource_property_request(const char *profile, const char *uuid, const char *property) {
   if (!profile || !*profile || !uuid || !*uuid || !property || !*property) {
      return NULL;
   }
   dict *request = dict_new();

   if (!request) {
      return NULL;
   }
   char *request_id = g_uuid_string_random();
   dict_add(request, "profile", profile);
   dict_add(request, "uuid", uuid);
   dict_add(request, "property", property);
   dict_add(request, "request.id", request_id);
   char *payload = dict2json(request);

   if (payload) {
      event_emit("client.resources.property-request", NULL, payload);
      free(payload);
   } else {
      g_clear_pointer(&request_id, g_free);
   }
   dict_free(request);

   return request_id;
}

typedef struct {
   char *profile, *uuid, *room, *property, *request_id;
   int original;
   gboolean suppress, known, read_only;
   GtkWidget *scale, *panel, *label, *revert;
   guint timer;
   unsigned attempts;
} resource_control_edit_t;

static void resource_control_send(resource_control_edit_t *edit, int value) {
   if (!edit || !edit->profile || !edit->uuid) {
      return;
   }
   dict *request = dict_new();

   if (!request) {
      return;
   }
   dict_add(request, "profile", edit->profile);
   dict_add(request, "target", edit->uuid);
   dict_add(request, "property", edit->property);
   dict_add(request, "room", edit->room ? edit->room : "");
   dict_add_int(request, "value", value);
   char *json = dict2json(request);

   if (json) {
      event_emit("client.resources.control-set", NULL, json);
      free(json);
   }
   dict_free(request);
}

static void resource_control_changed(GtkRange *range, gpointer user) {
   resource_control_edit_t *edit = user;

   if (!edit->suppress) {
      resource_control_send(edit, (int)gtk_range_get_value(range));
   }
}

static void resource_control_edit_free(gpointer data) {
   resource_control_edit_t *edit = data;

   if (edit->timer) {
      g_source_remove(edit->timer);
   }
   g_free(edit->profile);
   g_free(edit->uuid);
   g_free(edit->room);
   g_free(edit->property);
   g_free(edit->request_id);
   g_free(edit);
}
static gboolean resource_control_snapshot_poll(gpointer data) {
   resource_control_edit_t *edit = data;

   if (++edit->attempts > 50) {
      gtk_label_set_text(GTK_LABEL(edit->label), "Unable to read the current control value.");
      edit->timer = 0;

      return G_SOURCE_REMOVE;
   }
   const dict *snapshot = resource_property_cached(edit->profile, edit->uuid, edit->property, edit->request_id);

   if (!snapshot) {
      return G_SOURCE_CONTINUE;
   }

   if (!dict_get_bool((dict *) snapshot, "found", false) || !dict_get_bool((dict *) snapshot, "known", false)) {
      gtk_label_set_text(GTK_LABEL(edit->label), "The current value is not available from this rig.");
      edit->timer = 0;

      return G_SOURCE_REMOVE;
   }
   edit->original = CLAMP(atoi(dict_get((dict *) snapshot, "value", "50")), 0, 100);
   edit->known = true;
   edit->suppress = TRUE;
   gtk_range_set_value(GTK_RANGE(edit->scale), edit->original);
   edit->suppress = FALSE;
   gtk_widget_set_sensitive(edit->scale, !edit->read_only);
   gtk_widget_set_sensitive(edit->revert, !edit->read_only);
   gtk_label_set_text(GTK_LABEL(edit->label), edit->read_only ? "This rig reports the control as read-only." :
      "Changes apply immediately. Revert restores the original value.");
   edit->timer = 0;

   return G_SOURCE_REMOVE;
}
static void resource_control_revert(GtkButton *button, gpointer data) {
   (void)button;
   resource_control_edit_t *edit = data;

   if (!edit->known || edit->read_only) {
      return;
   }
   edit->suppress = TRUE;
   gtk_range_set_value(GTK_RANGE(edit->scale), edit->original);
   edit->suppress = FALSE;
   resource_control_send(edit, edit->original);
}
void resource_rig_control_dialog(GtkTreeModel *model, GtkTreeIter *item, const char *profile, const char *name, const char *property) {
   GtkTreeIter iter = *item, parent;
   char *uuid = NULL, *room = NULL, *details = NULL;
   gtk_tree_model_get(model, item, DETAILS, &details, -1);
   gboolean read_only = details && strstr(details, "read-only");
   while (gtk_tree_model_iter_parent(model, &parent, &iter)) {
      char *kind = NULL;
      gtk_tree_model_get(model, &parent, KIND, &kind, -1);

      if (!g_strcmp0(kind, "rig")) {
         gtk_tree_model_get(model, &parent, UUID, &uuid, ROOM, &room, -1);
         g_free(kind);
         break;
      }
      g_free(kind);
      iter = parent;
   }

   if (!uuid || !*uuid) {
      gtk_label_set_text(GTK_LABEL(browser->status), "Rig control has no owner UUID.");
      g_free(uuid);
      g_free(room);
      g_free(details);

      return;
   }
   resource_control_edit_t *edit = g_new0(resource_control_edit_t, 1);
   edit->profile = g_strdup(profile);
   edit->uuid = uuid;
   edit->room = room;
   edit->property = g_strdup(property);
   edit->read_only = read_only;
   edit->suppress = TRUE;

   GtkWidget *panel = NULL;
   GtkWidget *area = gtk_control_panel_open(name, &panel);

   if (!area) {
      resource_control_edit_free(edit);
      g_free(details);

      return;
   }
   edit->panel = panel;
   g_object_set_data_full(G_OBJECT(panel), "rr-rig-control-state", edit, resource_control_edit_free);
   GtkWidget *label = gtk_label_new("Reading the current control value…");
   edit->label = label;
   GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   edit->scale = scale;
   gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
   gtk_widget_set_size_request(scale, 320, -1);
   gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(area), scale, FALSE, FALSE, 0);
   g_signal_connect(scale, "value-changed", G_CALLBACK(resource_control_changed), edit);
   edit->suppress = FALSE;
   gtk_widget_set_sensitive(scale, FALSE);
   GtkWidget *actions = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_button_box_set_layout(GTK_BUTTON_BOX(actions), GTK_BUTTONBOX_END);
   GtkWidget *revert = gtk_button_new_with_mnemonic("_Revert");
   edit->revert = revert;
   GtkWidget *close = gtk_button_new_with_mnemonic("_Close");
   gtk_widget_set_sensitive(revert, FALSE);
   gtk_container_add(GTK_CONTAINER(actions), revert);
   gtk_container_add(GTK_CONTAINER(actions), close);
   gtk_box_pack_start(GTK_BOX(area), actions, FALSE, FALSE, 0);
   g_signal_connect(revert, "clicked", G_CALLBACK(resource_control_revert), edit);
   g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), panel);
   gtk_widget_show_all(panel);
   edit->request_id = resource_property_request(profile, uuid, property);

   if (edit->request_id) {
      edit->attempts = 0;
      edit->timer = g_timeout_add(100, resource_control_snapshot_poll, edit);
   } else {
      gtk_label_set_text(GTK_LABEL(label), "Unable to request the current control value.");
   }
   g_free(details);
}

typedef struct {
   char *profile, *uuid, *property, *request_id;
   GtkWidget *label;
   GtkWidget *selector;
   guint timer;
   unsigned attempts;
   char *initial_version;
   gboolean waiting_for_change;
} resource_menu_read_t;
static gboolean resource_menu_value_poll(gpointer data) {
   resource_menu_read_t *read = data;

   if (++read->attempts > 50) {
      gtk_label_set_text(GTK_LABEL(read->label), read->waiting_for_change ?
         "No CAT readback received. Check the server and rig CAT logs." : "No CAT value received. Check the server and rig CAT logs.");
      read->timer = 0;

      return G_SOURCE_REMOVE;
   }
   const dict *snapshot = resource_property_cached(read->profile, read->uuid, read->property, read->request_id);

   if (!snapshot) {
      return G_SOURCE_CONTINUE;
   }

   if (snapshot && dict_get_bool((dict *) snapshot, "found", false) &&
      dict_get_bool((dict *) snapshot, "known", false) && dict_get_bool((dict *) snapshot, "available", true)) {
      const char *version = dict_get((dict *) snapshot, "version", "");
      const char *value = dict_get((dict *) snapshot, "value", "");
      gboolean changed = !read->waiting_for_change || !read->initial_version || strcmp(version, read->initial_version);

      if (changed && *value) {
         char *summary = g_strdup_printf("CAT value: %s", value);
         gtk_label_set_text(GTK_LABEL(read->label), summary);

         if (read->selector && GTK_IS_ENTRY(read->selector) && strlen(value) <= 4 && strspn(value, "0123456789") == strlen(value)) {
            gtk_entry_set_text(GTK_ENTRY(read->selector), value);
         }
         g_free(summary);
         read->timer = 0;

         return G_SOURCE_REMOVE;
      }
   } else if (snapshot && !read->waiting_for_change) {
      gtk_label_set_text(GTK_LABEL(read->label), "The rig did not return a readable CAT value.");
      read->timer = 0;

      return G_SOURCE_REMOVE;
   }
   g_free(read->request_id);
   read->request_id = resource_property_request(read->profile, read->uuid, read->property);

   if (!read->request_id) {
      gtk_label_set_text(GTK_LABEL(read->label), "Unable to request a CAT value.");
      read->timer = 0;

      return G_SOURCE_REMOVE;
   }

   return G_SOURCE_CONTINUE;
}
static void resource_menu_read_free(gpointer data) {
   resource_menu_read_t *read = data;

   if (read->timer) {
      g_source_remove(read->timer);
   }
   g_free(read->profile);
   g_free(read->uuid);
   g_free(read->property);
   g_free(read->request_id);
   g_free(read->initial_version);
   g_free(read);
}
static void resource_menu_apply(GtkButton *button, gpointer data) {
   resource_menu_read_t *read = data;
   GtkWidget *selector = g_object_get_data(G_OBJECT(button), "menu-selector");
   const char *raw = GTK_IS_ENTRY(selector) ? gtk_entry_get_text(GTK_ENTRY(selector)) : NULL;

   if (!raw || !*raw || strlen(raw) > 4 || strspn(raw, "0123456789") != strlen(raw)) {
      gtk_label_set_text(GTK_LABEL(read->label), "Enter the numeric CAT parameter (preserving its leading zeros).");

      return;
   }
   const dict *cached = resource_property_cached(read->profile, read->uuid, read->property, read->request_id);
   const char *version = cached ? dict_get((dict *) cached, "version", NULL) : NULL;
   g_free(read->initial_version);
   read->initial_version = version ? g_strdup(version) : NULL;

   if (read->timer) {
      g_source_remove(read->timer);
      read->timer = 0;
   }
   dict *request = dict_new();

   if (!request) {
      return;
   }
   char *write_id = g_uuid_string_random();
   dict_add(request, "profile", read->profile);
   dict_add(request, "action", "menu-set");
   dict_add(request, "uuid", read->uuid);
   dict_add(request, "property", read->property);
   dict_add(request, "value", raw);
   dict_add(request, "request.id", write_id);
   char *payload = dict2json(request);

   if (payload) {
      event_emit("client.resources.action-request", NULL, payload);
      free(payload);
      gtk_label_set_text(GTK_LABEL(read->label), "Change submitted; awaiting CAT readback…");
      read->waiting_for_change = TRUE;
      read->attempts = 0;
      g_free(read->request_id);
      read->request_id = resource_property_request(read->profile, read->uuid, read->property);

      if (read->request_id) {
         read->timer = g_timeout_add(200, resource_menu_value_poll, read);
      } else {
         gtk_label_set_text(GTK_LABEL(read->label), "Unable to request CAT readback.");
      }
   }
   g_free(write_id);
   dict_free(request);
}
void resource_menu_open(GtkTreeModel *model, GtkTreeIter *item, const char *profile, const char *title, const char *id) {
   if (!profile || !id || strlen(id) != 4) {
      gtk_label_set_text(GTK_LABEL(browser->status), "Cannot open menu: missing server or menu ID.");

      return;
   }
   GtkTreeIter current = *item, parent;
   char *uuid = NULL;
   while (gtk_tree_model_iter_parent(model, &parent, &current)) {
      char *kind = NULL;
      gtk_tree_model_get(model, &parent, KIND, &kind, -1);

      if (!g_strcmp0(kind, "rig")) {
         gtk_tree_model_get(model, &parent, UUID, &uuid, -1);
         g_free(kind);
         break;
      }
      g_free(kind);
      current = parent;
   }

   if (!uuid || !*uuid) {
      g_free(uuid);

      return;
   }
   GtkWidget *panel = NULL, *area = gtk_control_panel_open(title, &panel);

   if (!area) {
      g_free(uuid);

      return;
   }
   GtkWidget *label = gtk_label_new("Reading one menu value from the rig…");
   gtk_label_set_xalign(GTK_LABEL(label), 0);
   gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 0);
   /* Numeric EX parameters are entered verbatim. The backend reads the current value, validates numeric width, writes and verifies it. */
   GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   GtkWidget *selector = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(selector), 4);
   gtk_entry_set_placeholder_text(GTK_ENTRY(selector), "CAT digits");
   gtk_widget_set_tooltip_text(selector, "Enter the exact numeric EX parameter, including leading zeros");
   GtkWidget *apply = gtk_button_new_with_label("Apply");
   gtk_box_pack_start(GTK_BOX(line), selector, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(line), apply, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(area), line, FALSE, FALSE, 0);
   GtkWidget *close = gtk_button_new_with_label("Close");
   gtk_box_pack_start(GTK_BOX(area), close, FALSE, FALSE, 0);
   g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), panel);
   resource_menu_read_t *read = g_new0(resource_menu_read_t, 1);
   read->profile = g_strdup(profile);
   read->uuid = uuid;
   read->property = g_strdup_printf("menu.%s", id);
   read->label = label;
   read->selector = selector;
   g_object_set_data_full(G_OBJECT(panel), "rr-menu-read", read, resource_menu_read_free);

   if (apply) {
      g_object_set_data(G_OBJECT(apply), "menu-selector", selector);
      g_signal_connect(apply, "clicked", G_CALLBACK(resource_menu_apply), read);
   }
   read->request_id = resource_property_request(profile, uuid, read->property);

   if (read->request_id) {
      read->timer = g_timeout_add(200, resource_menu_value_poll, read);
   } else {
      gtk_label_set_text(GTK_LABEL(label), "Unable to request a menu value.");
   }
   gtk_widget_show_all(panel);
}
