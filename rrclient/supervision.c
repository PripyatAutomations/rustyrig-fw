/* PARITY: www/js/webui.admin.js supervision state and room alerts. */
#include <string.h>
#include <stdlib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/frontend.h>
#include <rrclient/ui.h>
#include <rrclient/connman.h>
#include <rrclient/supervision.h>
struct grant {
   struct grant *next;
   rrconn_t *client;
   char room[128];
};
static struct grant *grants;
void rrclient_supervision_clear(rrconn_t *client) {
   for (struct grant **p = &grants; *p;) {
      struct grant *g = *p;
      if (g->client == client) { *p = g->next; free(g); } else p = &g->next;
   }
   if (frontend_ops() && frontend_ops()->supervision_result) frontend_ops()->supervision_result(NULL, client);
}
bool rrclient_supervision_granted(rrconn_t *client, const char *room) {
   for (struct grant *g = grants; g; g = g->next)
      if (g->client == client && room && !strcmp(g->room, room)) return true;
   return false;
}
void rrclient_supervision_result(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)user;
   dict *d = data ? json2dict(data) : NULL;
   if (!d) return;
   const char *room = dict_get(d, "talk.room", "");
   const char *action = dict_get(d, "talk.action", "");
   if (!strcmp(action, "granted") && !rrclient_supervision_granted(client, room)) {
      struct grant *g = calloc(1, sizeof(*g));
      if (g) { g->client = client; snprintf(g->room, sizeof(g->room), "%s", room); g->next = grants; grants = g; }
   } else if (!strcmp(action, "revoked") || !strcmp(action, "pending")) {
      for (struct grant **p = &grants; *p;) {
         struct grant *g = *p;
         if (g->client == client && !strcmp(g->room, room)) { *p = g->next; free(g); } else p = &g->next;
      }
   }
   ui_print(room, "SUPERVISION: %s — %s", dict_get(d, "talk.user", ""), dict_get(d, "talk.data", ""));
   if (!strcmp(action, "request")) ui_print(room,
      "Use /supervise accept %s %s to accept responsibility, or /supervise decline %s %s",
      dict_get(d, "talk.target", ""), room, dict_get(d, "talk.target", ""), room);
   if (frontend_ops() && frontend_ops()->supervision_result) frontend_ops()->supervision_result(data, client);
   dict_free(d);
}
