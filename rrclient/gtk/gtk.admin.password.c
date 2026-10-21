#include <gtk/gtk.h>
#include <string.h>
#include <rrclient/gtk/gtk.admin.password.h>

void gtk_admin_password_dialog(GtkWindow *parent, const char *profile, const char *name,
                               void (*submit)(void *context, const char *tail), void *context) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Set Password", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL,
      "Set Password", GTK_RESPONSE_OK, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   char *context = g_strdup_printf("Server: %s\nUser: %s\nPassword: 8–128 bytes, no whitespace", profile, name);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new(context), FALSE, FALSE, 6);
   g_free(context);
   GtkWidget *password = gtk_entry_new(), *confirmation = gtk_entry_new();
   gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);
   gtk_entry_set_visibility(GTK_ENTRY(confirmation), FALSE);
   gtk_entry_set_input_purpose(GTK_ENTRY(password), GTK_INPUT_PURPOSE_PASSWORD);
   gtk_entry_set_input_purpose(GTK_ENTRY(confirmation), GTK_INPUT_PURPOSE_PASSWORD);
   gtk_entry_set_placeholder_text(GTK_ENTRY(password), "New password");
   gtk_entry_set_placeholder_text(GTK_ENTRY(confirmation), "Confirm password");
   gtk_entry_set_activates_default(GTK_ENTRY(confirmation), TRUE);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
   gtk_box_pack_start(GTK_BOX(box), password, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(box), confirmation, FALSE, FALSE, 6);
   GtkWidget *error = gtk_label_new("");
   gtk_box_pack_start(GTK_BOX(box), error, FALSE, FALSE, 6);
   gtk_widget_show_all(dialog);

   while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      const char *value = gtk_entry_get_text(GTK_ENTRY(password));

      if (strcmp(value, gtk_entry_get_text(GTK_ENTRY(confirmation)))) {
         gtk_label_set_text(GTK_LABEL(error), "Passwords do not match");
         continue;
      }

      if (strlen(value) < 8 || strlen(value) > 128 || strpbrk(value, " \t\r\n\v\f")) {
         gtk_label_set_text(GTK_LABEL(error), "Use 8–128 bytes without whitespace");
         continue;
      }
      char *tail = g_strdup_printf("pass %s %s", name, value);
      submit(context, tail);
      memset(tail, 0, strlen(tail));
      g_free(tail);
      break;
   }
   gtk_widget_destroy(dialog);
}

