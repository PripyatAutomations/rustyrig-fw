// SPDX-License-Identifier: GPL-2.0-only
/* Server-owned graph configuration editor. Uses the same validated neutral
 * graph snapshot as the client and the existing admin request transport.
 * Deliberately does not expose a GTK-thread path to physical PTT. */
#include "gtk.server-routing.h"
#include "gtk.admin.command.h"
#include "gtk.control-graph.h"
#include <rrclient/connman.h>
#include <rrclient/control.graph.h>
#include <glib.h>
#include <string.h>

static GtkWidget *server_entry,*editor,*status,*revision_label,*save_button;
/* Both server-facing tabs select a specific cluster node, independent of
 * the active chat/rig connection. Free-form entry still accepts a profile
 * that joins the cluster after the tab is opened. */
GtkWidget *gtk_server_routing_server_selector(void) {
   GtkWidget *combo=gtk_combo_box_text_new_with_entry();
   for(unsigned i=0;i<128;i++) {
      const char *profile=rrclient_connection_iter(i);
      if(!profile)break;
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo),profile,profile);
   }
   const char *active=gtk_admin_selected_profile();
   if(!active||!*active)active=rrclient_selected_server();
   GtkWidget *entry=gtk_bin_get_child(GTK_BIN(combo));
   if(active&&*active)gtk_entry_set_text(GTK_ENTRY(entry),active);
   gtk_entry_set_max_length(GTK_ENTRY(entry),127);
   gtk_widget_set_tooltip_text(combo,"Choose a connected cluster server profile");
   return combo;
}

static char requested_profile[128];
static gint64 current_revision;
static gboolean may_edit,loaded;

static gchar *editor_text(void) {
   GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
   GtkTextIter start,end;
   gtk_text_buffer_get_bounds(buffer,&start,&end);
   return gtk_text_buffer_get_text(buffer,&start,&end,FALSE);
}
static void set_status(const char *value) {
   if(status)gtk_label_set_text(GTK_LABEL(status),value);
}
static const char *selected(void) {
   GtkWidget *entry=gtk_bin_get_child(GTK_BIN(server_entry));
   const char *value=gtk_entry_get_text(GTK_ENTRY(entry));
   return value?value:"";
}
static void server_changed(GtkComboBox *combo,gpointer user) {
   (void)combo;(void)user;
   loaded=FALSE;
   may_edit=FALSE;
   if(save_button)gtk_widget_set_sensitive(save_button,FALSE);
   set_status("Server selection changed; Refresh before editing or saving");
}
static void fetch(GtkButton *button,gpointer user) {
   (void)button;(void)user;
   const char *profile=selected();
   if(!*profile) {
      set_status("Enter a connected server profile name");
      return;
   }
   g_strlcpy(requested_profile,profile,sizeof(requested_profile));
   loaded=FALSE;
   gtk_widget_set_sensitive(save_button,FALSE);
   if(gtk_admin_command_event(status,profile,"routing-get","",NULL,NULL))
      set_status("Requesting server-owned routing graph…");
}
static void stage(GtkButton *button,gpointer user) {
   (void)button;(void)user;
   gchar *source=rr_logic_canvas_export();
   if(!source||!*source) {
      set_status("Client Routing graph has no saved configuration");
   } else {
      GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
      gtk_text_buffer_set_text(buffer,source,-1);
      set_status("Client graph staged locally; validate and save explicitly");
   }
   g_free(source);
}
static gboolean validate_text(const char *text) {
   char *error=NULL;
   gboolean valid=rr_control_graph_text_validate(text,&error);
   set_status(valid ? "Graph topology valid; physical outputs stay disconnected" :
      (error?error:"Invalid graph"));
   g_free(error);
   return valid;
}
static void validate(GtkButton *button,gpointer user) {
   (void)button;(void)user;
   gchar *value=editor_text();
   validate_text(value);
   g_free(value);
}
static void store(GtkButton *button,gpointer user) {
   (void)button;(void)user;
   if(!loaded||!may_edit) {
      set_status("Refresh first; owner/superadmin required to save");
      return;
   }
   gchar *value=editor_text();
   if(!validate_text(value)) {g_free(value);return;}
   char revision[32];
   g_snprintf(revision,sizeof(revision),"%"G_GINT64_FORMAT,current_revision);
   if(gtk_admin_command_event(status,requested_profile,"routing-put",
                              value,revision,NULL))
      set_status("Submitting version-checked server graph configuration…");
   g_free(value);
}
static void apply_client(GtkButton *button,gpointer user) {
   (void)button;(void)user;
   gchar *value=editor_text();
   if(!validate_text(value)) {
      g_free(value);
      return;
   }
   if(rr_logic_canvas_import(value))
      set_status("Graph applied to client canvas; no server or PTT state changed");
   else
      set_status("Client import failed; prior graph retained");
   g_free(value);
}
static void copy_graph(GtkButton *button,gpointer user) {
   (void)button;(void)user;
   gchar *value=editor_text();
   GtkClipboard *clip=gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
   gtk_clipboard_set_text(clip,value,-1);
   g_free(value);
   set_status("Graph snapshot copied; no server state changed");
}
GtkWidget *gtk_server_routing_page(void) {
   GtkWidget *page=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
   gtk_container_set_border_width(GTK_CONTAINER(page),8);
   GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
   GtkWidget *label=gtk_label_new("Server profile:");
   server_entry=gtk_server_routing_server_selector();
   gtk_entry_set_placeholder_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(server_entry))),
      "Select cluster server");
   gtk_widget_set_hexpand(server_entry,TRUE);

   GtkWidget *refresh=gtk_button_new_with_label("Refresh");
   GtkWidget *stage_button=gtk_button_new_with_label("Stage Client Graph");
   GtkWidget *check=gtk_button_new_with_label("Validate");
   save_button=gtk_button_new_with_label("Save to Server");
   GtkWidget *copy=gtk_button_new_with_label("Copy");
   GtkWidget *apply=gtk_button_new_with_label("Apply to Client");
   gtk_widget_set_sensitive(save_button,FALSE);
   gtk_box_pack_start(GTK_BOX(row),label,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(row),server_entry,TRUE,TRUE,0);
   gtk_box_pack_start(GTK_BOX(row),refresh,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(row),stage_button,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(row),check,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(row),save_button,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(row),copy,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(row),apply,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(page),row,FALSE,FALSE,0);
   GtkWidget *notice=gtk_label_new(
      "Server Routing (configuration stage): graph editing is versioned and "
      "server-authorized. Saving does NOT connect physical outputs or key PTT.");
   gtk_label_set_line_wrap(GTK_LABEL(notice),TRUE);
   gtk_label_set_xalign(GTK_LABEL(notice),0.f);
   gtk_box_pack_start(GTK_BOX(page),notice,FALSE,FALSE,0);
   GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
   editor=gtk_text_view_new();
   gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor),TRUE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor),GTK_WRAP_NONE);
   gtk_container_add(GTK_CONTAINER(scroll),editor);
   gtk_widget_set_hexpand(scroll,TRUE);
   gtk_widget_set_vexpand(scroll,TRUE);
   gtk_box_pack_start(GTK_BOX(page),scroll,TRUE,TRUE,0);
   GtkWidget *footer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
   status=gtk_label_new("Choose a server and Refresh");
   gtk_label_set_xalign(GTK_LABEL(status),0.f);
   gtk_widget_set_hexpand(status,TRUE);
   revision_label=gtk_label_new("Revision: —");
   gtk_box_pack_start(GTK_BOX(footer),status,TRUE,TRUE,0);
   gtk_box_pack_end(GTK_BOX(footer),revision_label,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(page),footer,FALSE,FALSE,0);
   g_signal_connect(server_entry,"changed",G_CALLBACK(server_changed),NULL);
   g_signal_connect(refresh,"clicked",G_CALLBACK(fetch),NULL);
   g_signal_connect(stage_button,"clicked",G_CALLBACK(stage),NULL);
   g_signal_connect(check,"clicked",G_CALLBACK(validate),NULL);
   g_signal_connect(save_button,"clicked",G_CALLBACK(store),NULL);
   g_signal_connect(copy,"clicked",G_CALLBACK(copy_graph),NULL);
   g_signal_connect(apply,"clicked",G_CALLBACK(apply_client),NULL);
   return page;
}
gboolean gtk_server_routing_result(const char *table,const char *action,
                                   dict *row,const char *profile) {
   if(g_strcmp0(table,"server-routing"))return FALSE;
   if(!row||!editor||g_strcmp0(action,"snapshot")||
      g_strcmp0(profile,requested_profile))return TRUE;
   const char *data=dict_get(row,"data","");
   const char *rev=dict_get(row,"revision","0");
   char *end=NULL;
   gint64 number=g_ascii_strtoll(rev,&end,10);
   if(!end||*end||number<0)return TRUE;
   current_revision=number;
   may_edit=!g_strcmp0(dict_get(row,"writable","false"),"true");
   loaded=TRUE;
   gtk_widget_set_sensitive(save_button,may_edit);
   GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
   gtk_text_buffer_set_text(buffer,data,-1);
   gchar *title=g_strdup_printf("Revision: %"G_GINT64_FORMAT,current_revision);
   gtk_label_set_text(GTK_LABEL(revision_label),title);
   g_free(title);
   set_status(may_edit?
      "Server graph loaded (owner/superadmin; inactive until runtime integration)":
      "Server graph loaded read-only; owner/superadmin required to edit");
   return TRUE;
}

/* Server pipeline topology is not exposed by rrserver yet. Keep the
 * second server tab and its cluster selector separate from the client
 * GStreamer pipeline model: displaying client elements here would imply
 * they belong to the selected server. */
GtkWidget *gtk_server_pipelines_page(void) {
   GtkWidget *page=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
   gtk_container_set_border_width(GTK_CONTAINER(page),8);
   GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
   GtkWidget *label=gtk_label_new("Server:");
   GtkWidget *server=gtk_server_routing_server_selector();
   gtk_widget_set_hexpand(server,TRUE);
   gtk_box_pack_start(GTK_BOX(row),label,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(row),server,TRUE,TRUE,0);
   gtk_box_pack_start(GTK_BOX(page),row,FALSE,FALSE,0);
   GtkWidget *notice=gtk_label_new(
      "Srv Pipelines — server pipeline topology is not available yet. "
      "This view will use the shared Pipeline editor when rrserver exposes "
      "its pipeline inventory and authenticated revisioned edit/apply API. "
      "The local Pipelines tab continues to edit client-owned GStreamer pipelines.");
   gtk_label_set_xalign(GTK_LABEL(notice),0.);
   gtk_label_set_line_wrap(GTK_LABEL(notice),TRUE);
   gtk_box_pack_start(GTK_BOX(page),notice,FALSE,FALSE,8);
   return page;
}
