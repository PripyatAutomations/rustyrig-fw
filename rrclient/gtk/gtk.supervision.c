// Supervision prompt lifecycle is independent of admin tables.
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.ptt-btn.h>

/* Nonblocking: several elmers/noobs and background server profiles may coexist. */
typedef struct {
   GtkWidget *dialog;
   char *profile, *id, *room;
   rrconn_t *connection;
} supervision_dialog_t;
static GList *supervision_dialogs;
static void supervision_response(GtkDialog *dialog, gint response, gpointer data) {
   supervision_dialog_t *prompt = data;
   rrconn_t *client = rrclient_connection_find(prompt->profile);

   if ((response == GTK_RESPONSE_YES || response == GTK_RESPONSE_NO) && client == prompt->connection && client->authenticated) {
      dict *d = dict_new();

      if (d) {
         char tail[96];
         snprintf(tail, sizeof(tail), "%s %s", response == GTK_RESPONSE_YES ? "accept" : "decline", prompt->id);
         dict_add(d, "msg.type", "talk");
         dict_add(d, "talk.cmd", "supervise");
         dict_add(d, "talk.room", prompt->room);
         dict_add(d, "talk.data", tail);
         ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
         dict_free(d);
      }
   }
   supervision_dialogs = g_list_remove(supervision_dialogs, prompt);
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(prompt->profile);
   g_free(prompt->id);
   g_free(prompt->room);
   g_free(prompt);
}
void gtk_supervision_result(const char *data, rrconn_t *connection) {
   dict *d = data ? json2dict(data) : NULL;
   const char *id = d ? dict_get(d, "talk.target", "") : "";
   const char *action = d ? dict_get(d, "talk.action", "") : "closed";

   for (GList *p = supervision_dialogs, *next ; p ; p = next) {
      next = p->next;
      supervision_dialog_t *prompt = p->data;

      if (prompt->connection == connection && (!d || !strcmp(prompt->id, id))) {
         if (!strcmp(action, "request")) {
            dict_free(d);

            return;
         }
         gtk_dialog_response(GTK_DIALOG(prompt->dialog), GTK_RESPONSE_CANCEL);
      }
   }

   if (d && !strcmp(action, "request")) {
      supervision_dialog_t *prompt = g_new0(supervision_dialog_t, 1);
      prompt->profile = g_strdup(rrclient_connection_name(connection));
      prompt->connection = connection;
      prompt->id = g_strdup(id);
      prompt->room = g_strdup(dict_get(d, "talk.room", ""));
      prompt->dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
         "Supervise %s in %s (%s)?", dict_get(d, "talk.user", ""), prompt->room, prompt->profile);
      gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(prompt->dialog), "%s", dict_get(d, "talk.data", ""));
      gtk_dialog_add_buttons(GTK_DIALOG(prompt->dialog), "Decline", GTK_RESPONSE_NO, "Accept responsibility", GTK_RESPONSE_YES, NULL);
      g_signal_connect(prompt->dialog, "response", G_CALLBACK(supervision_response), prompt);
      supervision_dialogs = g_list_prepend(supervision_dialogs, prompt);
      gtk_widget_show(prompt->dialog);
   }

   if (rrclient_present_context()) {
      ptt_button_refresh();
   }
   dict_free(d);
}
