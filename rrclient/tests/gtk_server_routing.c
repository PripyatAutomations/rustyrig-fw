// SPDX-License-Identifier: GPL-2.0-only
/* Server Routing request/selection isolation; no live cluster required. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <rrclient/gtk/gtk.server-routing.c>

static const char *last_profile, *last_action;
static unsigned requests;
static gboolean allow_requests=TRUE;

const char *rrclient_connection_iter(unsigned index) {
   return index==0?"node-a":index==1?"node-b":NULL;
}
const char *gtk_admin_selected_profile(void) { return "node-a"; }
const char *rrclient_selected_server(void) { return "node-a"; }
bool gtk_admin_command_event(GtkWidget *label,const char *profile,
                             const char *action,const char *tail,
                             const char *target,dict *fields) {
   (void)label;(void)tail;(void)target;(void)fields;
   last_profile=profile;
   last_action=action;
   requests++;
   return allow_requests;
}
gchar *rr_logic_canvas_export(void) { return g_strdup("graph"); }
gboolean rr_logic_canvas_import(const char *text) { (void)text;return TRUE; }
gboolean rr_control_graph_text_validate(const char *text,char **error) {
   (void)error;
   return text && *text;
}
static void reply(const char *profile,const char *revision,gboolean writable,
                  const char *data) {
   dict *row=dict_new();
   dict_add(row,"revision",revision);
   dict_add(row,"writable",writable?"true":"false");
   dict_add(row,"data",data);
   assert(gtk_server_routing_result("server-routing","snapshot",row,profile));
   dict_free(row);
}
static void choose(const char *name) {
   gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(server_entry))),name);
   /* Both editable combo entry and combo selection must invalidate state. */
   server_changed(GTK_COMBO_BOX(server_entry),NULL);
}
static void check_text(const char *want) {
   gchar *value=editor_text();
   assert(!strcmp(value,want));
   g_free(value);
}
int main(void) {
   if(!gtk_init_check(NULL,NULL)) {
      puts("SKIP: GTK display unavailable");
      return 0;
   }
   GtkWidget *page=gtk_server_routing_page();
   assert(page && save_button && !gtk_widget_get_sensitive(save_button));
   assert(!strcmp(selected(),"node-a"));

   fetch(NULL,NULL);
   assert(requests==1 && !strcmp(last_profile,"node-a") &&
          !strcmp(last_action,"routing-get"));
   reply("node-a","7",TRUE,"first");
   assert(loaded && may_edit && current_revision==7);
   assert(gtk_widget_get_sensitive(save_button));
   check_text("first");

   choose("node-b");
   assert(!loaded && !may_edit && current_revision==0);
   assert(!gtk_widget_get_sensitive(save_button));
   reply("node-a","8",TRUE,"stale");
   assert(!loaded && !gtk_widget_get_sensitive(save_button));
   check_text("first");
   fetch(NULL,NULL);
   assert(requests==2 && !strcmp(last_profile,"node-b"));
   reply("node-a","9",TRUE,"stale");
   assert(!loaded);
   reply("node-b","10",FALSE,"readonly");
   assert(loaded && !may_edit && current_revision==10);
   assert(!gtk_widget_get_sensitive(save_button));
   check_text("readonly");

   store(NULL,NULL);
   assert(requests==2); /* read-only server cannot be saved */
   choose("node-a");
   reply("node-b","11",TRUE,"stale");
   assert(!loaded);
   allow_requests=FALSE;
   fetch(NULL,NULL);
   assert(requests==3 && !*requested_profile);
   reply("node-a","12",TRUE,"stale");
   assert(!loaded);
   allow_requests=TRUE;
   fetch(NULL,NULL);
   reply("node-a","13",TRUE,"final");
   assert(loaded && may_edit && current_revision==13);
   store(NULL,NULL);
   assert(requests==5 && !strcmp(last_action,"routing-put") &&
          !strcmp(last_profile,"node-a"));
   gtk_widget_destroy(page);
   puts("PASS: server Routing selection, stale replies, read-only and rejected refresh");
   return 0;
}
