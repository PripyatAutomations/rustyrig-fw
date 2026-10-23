// Supervision prompt lifecycle is independent of admin tables.
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.ptt-btn.h>
#include "gtk.floating-panel.h"
extern GtkWidget *main_notebook;

/* Nonblocking: several elmers/noobs and background server profiles may coexist. */
typedef struct {
   RRFloatPanel *dialog;
   char *profile, *id, *room;
   rrconn_t *connection;
} supervision_dialog_t;
static GList *supervision_dialogs;
static void supervision_response(RRFloatPanel *dialog, gint response, gpointer data) {
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
   rr_floating_panel_destroy(dialog);
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
         rr_floating_panel_response(prompt->dialog, GTK_RESPONSE_CANCEL);
      }
   }

   if (d && !strcmp(action, "request")) {
      supervision_dialog_t *prompt = g_new0(supervision_dialog_t, 1);
      prompt->profile = g_strdup(rrclient_connection_name(connection));
      prompt->connection = connection;
      prompt->id = g_strdup(id);
      prompt->room = g_strdup(dict_get(d, "talk.room", ""));
      GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;
      if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) {
         g_free(prompt->profile); g_free(prompt->id); g_free(prompt->room); g_free(prompt);
         dict_free(d);
         return;
      }
      char *panel_id = g_strdup_printf("supervision-%s-%s", prompt->profile, prompt->id);
      prompt->dialog = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook),
                                              panel_id, "Supervision request");
      g_free(panel_id);
      if (!prompt->dialog) {
         g_free(prompt->profile); g_free(prompt->id); g_free(prompt->room); g_free(prompt);
         dict_free(d);
         return;
      }
      GtkWidget *body = rr_floating_panel_body(prompt->dialog);
      char *question = g_strdup_printf("Supervise %s in %s (%s)?",
             dict_get(d, "talk.user", ""), prompt->room, prompt->profile);
      GtkWidget *heading = gtk_label_new(question);
      g_free(question);
      gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
      gtk_box_pack_start(GTK_BOX(body), heading, FALSE, FALSE, 4);
      GtkWidget *details = gtk_label_new(dict_get(d, "talk.data", ""));
      gtk_label_set_line_wrap(GTK_LABEL(details), TRUE);
      gtk_label_set_xalign(GTK_LABEL(details), 0.0);
      gtk_box_pack_start(GTK_BOX(body), details, FALSE, FALSE, 4);
      GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
      gtk_box_pack_start(GTK_BOX(body), actions, FALSE, FALSE, 4);
      rr_floating_panel_add_response(prompt->dialog, actions, "Accept responsibility", GTK_RESPONSE_YES);
      rr_floating_panel_add_response(prompt->dialog, actions, "Decline", GTK_RESPONSE_NO);
      rr_floating_panel_set_modal(prompt->dialog, TRUE);
      rr_floating_panel_set_response_callback(prompt->dialog, supervision_response, prompt);
      supervision_dialogs = g_list_prepend(supervision_dialogs, prompt);
      rr_floating_panel_show(prompt->dialog);
   }

   if (rrclient_present_context()) {
      ptt_button_refresh();
   }
   dict_free(d);
}
