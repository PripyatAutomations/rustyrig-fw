// Privilege-set editing is separate from the admin table controller.
#include <gtk/gtk.h>
#include <string.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.privileges.h>
#include <rrclient/gtk/gtk.admin.privgroup.h>

static bool (*privgroup_sender)(admin_table_t *, const char *, const char *, const char *);

void gtk_admin_privgroup_set_sender(bool (*sender)(admin_table_t *, const char *, const char *, const char *)) {
   privgroup_sender = sender;
}

static bool privgroup_command(admin_table_t *table, const char *command, const char *tail, const char *target) {
   if (!privgroup_sender) {
      return false;
   }
   return privgroup_sender(table, command, tail, target);
}

static void gtk_admin_privgroup_edit(GtkButton *button,gpointer user) {
   admin_table_t *table=user;
   const char *operation=g_object_get_data(G_OBJECT(button),"privgroup-operation");
   if (!operation) return;
   GtkWidget *dialog=gtk_dialog_new_with_buttons("Privilege Group",
      GTK_WINDOW(main_window),GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_OK,NULL);
   GtkWidget *area=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *group=gtk_entry_new();
   GtkWidget *privilege=gtk_entry_new();
   GtkWidget *description=gtk_entry_new();
   GtkWidget *deny=gtk_check_button_new_with_label("Invert Rule (Deny access)");
   const char *editing_group=!strcmp(table->name,"setentries") ?
      g_object_get_data(G_OBJECT(table->tree),"rr-editing-group") : NULL;
   GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree));
   GtkTreeModel *model=NULL;GtkTreeIter iter;
   if (gtk_tree_selection_get_selected(selection,&model,&iter)) {
      char *name=NULL,*token=NULL;
      gtk_tree_model_get(model,&iter,0,&name,-1);
      if (name) gtk_entry_set_text(GTK_ENTRY(group),name);
      if (!strcmp(table->name,"setentries")) {
         gtk_tree_model_get(model,&iter,1,&token,-1);
         if (token) gtk_entry_set_text(GTK_ENTRY(privilege),token);
      }
      g_free(name);g_free(token);
   }
   if (editing_group && *editing_group) {
      gtk_entry_set_text(GTK_ENTRY(group),editing_group);
      gtk_editable_set_editable(GTK_EDITABLE(group),FALSE);
   }
   gtk_entry_set_placeholder_text(GTK_ENTRY(group),"e.g. operators");
   gtk_entry_set_placeholder_text(GTK_ENTRY(privilege),"e.g. rig.use");
   gtk_box_pack_start(GTK_BOX(area),gtk_label_new("Group name"),FALSE,FALSE,2);
   gtk_box_pack_start(GTK_BOX(area),group,FALSE,FALSE,2);
   if (!strcmp(operation,"privset-create")) {
      gtk_box_pack_start(GTK_BOX(area),gtk_label_new("Description"),FALSE,FALSE,2);
      gtk_box_pack_start(GTK_BOX(area),description,FALSE,FALSE,2);
   } else if (!strcmp(operation,"privset-entry") || !strcmp(operation,"privset-unentry")) {
      gtk_box_pack_start(GTK_BOX(area),gtk_label_new("Privilege"),FALSE,FALSE,2);
      gtk_box_pack_start(GTK_BOX(area),privilege,FALSE,FALSE,2);
      if (!strcmp(operation,"privset-entry"))
         gtk_box_pack_start(GTK_BOX(area),deny,FALSE,FALSE,2);
   }
   gtk_widget_show_all(dialog);
   if (gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_OK) {
      const char *name=gtk_entry_get_text(GTK_ENTRY(group));
      const char *token=gtk_entry_get_text(GTK_ENTRY(privilege));
      const char *desc=gtk_entry_get_text(GTK_ENTRY(description));
      char *tail=NULL;
      if (!strcmp(operation,"privset-create")) tail=g_strdup(desc);
      else if (!strcmp(operation,"privset-delete")) tail=g_strdup("");
      else if (*token && !strpbrk(token," \t\r\n"))
         tail=!strcmp(operation,"privset-entry") ?
            g_strdup_printf("%s %s",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(deny)) ?
               "deny" : "allow",token) : g_strdup(token);
      if (*name && !strpbrk(name," \t\r\n") && tail) {
         if (privgroup_command(table,operation,tail,name))
            gtk_label_set_text(GTK_LABEL(table->status),"Privilege group change submitted; refresh to view");
      } else gtk_label_set_text(GTK_LABEL(table->status),"Valid group and privilege names are required");
      g_free(tail);
   }
   gtk_widget_destroy(dialog);
}
/* Same privilege checklist as Admin / Users; persist individual set entries. */
static void gtk_admin_privgroup_open(admin_table_t *table) {
   if (!table || !table->tree) return;
   GtkTreeModel *model=NULL;
   GtkTreeIter iter;
   if (!gtk_tree_selection_get_selected(
       gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree)),&model,&iter)) return;
   char *group=NULL,*current=NULL;
   gtk_tree_model_get(model,&iter,0,&group,4,&current,-1);
   if (!group || !*group) { g_free(group); g_free(current); return; }
   char *updated=gtk_edit_group_privileges(GTK_WINDOW(main_window),table->profile,group,current);
   if (updated) {
      char **before=g_strsplit(current ? current : "",",",-1);
      char **after=g_strsplit(updated,",",-1);
      GHashTable *wanted=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
      GHashTable *existing=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
      for (char **p=after;*p;p++) if (**p) g_hash_table_add(wanted,g_strdup(*p));
      for (char **p=before;*p;p++) if (**p) g_hash_table_add(existing,g_strdup(*p));
      for (char **p=before;*p;p++) {
         if (!**p || g_hash_table_contains(wanted,*p)) continue;
         const char *token=**p=='!' ? *p+1 : *p;
         if (*token) privgroup_command(table,"privset-unentry",token,group);
      }
      for (char **p=after;*p;p++) {
         if (!**p || g_hash_table_contains(existing,*p)) continue;
         const char *token=**p=='!' ? *p+1 : *p;
         if (*token) {
            char *tail=g_strdup_printf("%s %s",**p=='!' ? "deny":"allow",token);
            privgroup_command(table,"privset-entry",tail,group);
            g_free(tail);
         }
      }
      g_hash_table_destroy(wanted);
      g_hash_table_destroy(existing);
      g_strfreev(before);
      g_strfreev(after);
      gtk_label_set_text(GTK_LABEL(table->status),"Group privileges submitted; refresh to confirm");
      g_free(updated);
   }
   g_free(current);
   g_free(group);
}
static void gtk_admin_privgroup_open_clicked(GtkButton *button,gpointer user) {
   (void)button;
   gtk_admin_privgroup_open(user);
}
static void gtk_admin_privgroup_row_activated(GtkTreeView *view,GtkTreePath *path,
                                          GtkTreeViewColumn *column,gpointer user) {
   (void)column;
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(view),path);
   gtk_admin_privgroup_open(user);
}
static gboolean gtk_admin_privgroup_context_menu(GtkWidget *tree,GdkEventButton *event,gpointer user) {
   if (event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;
   GtkTreePath *path=NULL;
   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree),
       (int)event->x,(int)event->y,&path,NULL,NULL,NULL)) return FALSE;
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),path);
   gtk_tree_path_free(path);
   GtkWidget *menu=gtk_menu_new();
   GtkWidget *edit=gtk_menu_item_new_with_label("Edit Privs");
   g_signal_connect(edit,"activate",G_CALLBACK(gtk_admin_privgroup_open_clicked),user);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),edit);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);
   return TRUE;
}
