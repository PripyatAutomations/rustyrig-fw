// GTK privilege selection. Part of rustyrig-fw; MIT without mongoose, GPL with it.
#include <gtk/gtk.h>
#include <string.h>
#include <rrclient/privileges.generated.h>
#include <rrclient/gtk/gtk.privileges.h>

/* PARITY: rustyrig-www/js/webui.admin.js adminEditPrivileges. */
static char *edit_privileges(GtkWindow *parent, const char *server, const char *name, const char *current, gboolean group_mode) {
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons(group_mode ? "Edit Group Privileges" : "Edit User Privileges", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "Cancel", GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_OK, NULL);
   gtk_window_set_default_size(GTK_WINDOW(dialog), 440, 520);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   char *context = group_mode ?
      g_strdup_printf("Server: %s\nPrivilege group: %s\nChoose the privileges included in this group (nested groups are not permitted).", server ? server : "",
      name) : g_strdup_printf("Server: %s\nUser: %s\nProtected accounts and grants require owner privileges.", server ? server : "", name);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new(context), FALSE, FALSE, 8);
   g_free(context);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL), *rows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_container_set_border_width(GTK_CONTAINER(rows), 10);
   gtk_container_add(GTK_CONTAINER(scroll), rows);
   gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
   GHashTable *selected = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   GList *names = NULL;

   for (unsigned i = 0 ; i < G_N_ELEMENTS(rrclient_known_privileges) ; i++) {
      names = g_list_prepend(names, g_strdup(rrclient_known_privileges[i]));
   }

   char **existing = g_strsplit(current ? current : "", ",", -1);

   /* Old databases may contain unsupported nested references. Do not hide them and silently erase them when the operator clicks Apply. */
   if (group_mode) {
      for (char **item = existing ; *item ; item++) {
         if (**item == '@') {
            GtkWidget *warning = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
               "Group %s contains the nested group reference %s", name, *item);
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(warning),
               "Nested groups are not supported. Remove the legacy entry before editing this group.");
            gtk_dialog_run(GTK_DIALOG(warning));
            gtk_widget_destroy(warning);
            g_strfreev(existing);
            g_list_free_full(names, g_free);
            g_hash_table_destroy(selected);
            gtk_widget_destroy(dialog);

            return NULL;
         }
      }
   }

   for (char **item = existing ; *item ; item++) {
      if (!**item || !g_ascii_strcasecmp(*item, "none")) {
         continue;
      }

      /* Keep custom/export-specific grants visible and selected, never silently remove them. */
      if (!g_list_find_custom(names, *item, (GCompareFunc) g_strcmp0)) {
         names = g_list_prepend(names, g_strdup(*item));
      }
      g_hash_table_add(selected, g_strdup(*item));
   }

   g_strfreev(existing);
   names = g_list_sort(names, (GCompareFunc) g_strcmp0);
   GPtrArray *buttons = g_ptr_array_new();

   for (GList *item = names ; item ; item = item->next) {
      /* Group references (@name) are valid user assignments, but group definitions must not recursively contain other groups. */
      if (group_mode && ((const char *)item->data)[0] == '@') {
         continue;
      }
      GtkWidget *button = gtk_check_button_new_with_label(item->data);

      for (unsigned j = 0 ; j < G_N_ELEMENTS(rrclient_privilege_registry) ; ++j) {
         const rr_privilege_info_t *info = &rrclient_privilege_registry[j];

         if (!strcmp(info->name, item->data)) {
            char *tip = g_strdup_printf("%s — %s (%s)", info->label, info->description, info->scope);
            gtk_widget_set_tooltip_text(button, tip);
            g_free(tip);
            break;
         }
      }

      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), g_hash_table_contains(selected, item->data));
      gtk_box_pack_start(GTK_BOX(rows), button, FALSE, FALSE, 0);
      g_ptr_array_add(buttons, button);
   }

   gtk_widget_show_all(dialog);
   char *result = NULL;

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      GString *privileges = g_string_new("");

      for (unsigned i = 0 ; i < buttons->len ; i++) {
         GtkWidget *button = g_ptr_array_index(buttons, i);

         if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) {
            continue;
         }

         if (privileges->len) {
            g_string_append_c(privileges, ',');
         }
         g_string_append(privileges, gtk_button_get_label(GTK_BUTTON(button)));
      }

      result = g_string_free(privileges, FALSE);
   }
   g_ptr_array_free(buttons, TRUE);
   g_list_free_full(names, g_free);
   g_hash_table_destroy(selected);
   gtk_widget_destroy(dialog);

   return result;
}

char *gtk_edit_privileges(GtkWindow *parent, const char *server, const char *name, const char *current) {
   return edit_privileges(parent, server, name, current, FALSE);
}

char *gtk_edit_group_privileges(GtkWindow *parent, const char *server, const char *group, const char *current) {
   return edit_privileges(parent, server, group, current, TRUE);
}
