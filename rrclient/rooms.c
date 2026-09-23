#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/rooms.h>
#ifdef USE_GTK
#include <gtk/gtk.h>
#include <rrclient/gtk.chat.h>
#endif

extern rrconn_t *ws_conn;

typedef struct client_room {
   char name[128];
   char vfos[512];
   struct client_room *next;
} client_room_t;

static client_room_t *rooms;

static const char *canonical(const char *room) {
   return (room && *room) ? room : ws_authoritative_room();
}

bool rrclient_room_is_joined(const char *room) {
   const char *want = canonical(room);
   for (client_room_t *r = rooms; r; r = r->next)
      if (!strcasecmp(r->name, want)) return true;
   return false;
}

bool rrclient_room_join(const char *room) {
   const char *name = canonical(room);
   if (!name || !*name || rrclient_room_is_joined(name)) return true;
   client_room_t *r = calloc(1, sizeof(*r));
   if (!r) return false;
   strlcpy(r->name, name, sizeof(r->name));
   r->next = rooms;
   rooms = r;
   return true;
}

bool rrclient_room_request_join(const char *room) {
   if (!room || !*room || !ws_conn || rrclient_room_is_joined(room)) return false;
   dict *d = dict_new();
   if (!d) return false;
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "join");
   dict_add(d, "talk.target", room);
   bool ok = ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   return ok;
}

bool rrclient_room_part(const char *room) {
   const char *name = canonical(room);
   client_room_t **pp = &rooms;
   while (*pp) {
      client_room_t *r = *pp;
      if (!strcasecmp(r->name, name)) {
         *pp = r->next;
         free(r);
         return true;
      }
      pp = &r->next;
   }
   return false;
}

void rrclient_rooms_clear(void) {
   while (rooms) {
      client_room_t *next = rooms->next;
      free(rooms);
      rooms = next;
   }
}

bool rrclient_room_set_vfos(const char *room, const char *vfos) {
   if (!rrclient_room_join(room)) return false;
   const char *name = canonical(room);
   for (client_room_t *r = rooms; r; r = r->next) {
      if (!strcasecmp(r->name, name)) { strlcpy(r->vfos, vfos ? vfos : "", sizeof(r->vfos)); return true; }
   }
   return false;
}

bool rrclient_room_set_vfo_mask(const char *room, unsigned long mask) {
   char vfos[256] = "";
   size_t used = 0;
   for (unsigned int i = 0; i < 32; i++) {
      if (!(mask & (1UL << i))) continue;
      int n = snprintf(vfos + used, sizeof(vfos) - used, "%srig0.vfo_%c",
         used ? " " : "", (char)('a' + i));
      if (n < 0 || (size_t)n >= sizeof(vfos) - used) break;
      used += (size_t)n;
   }
   return rrclient_room_set_vfos(room, vfos);
}

const char *rrclient_room_vfos(const char *room) {
   const char *name = canonical(room);
   for (client_room_t *r = rooms; r; r = r->next)
      if (!strcasecmp(r->name, name)) return r->vfos;
   return "";
}

const char *rrclient_current_room(void) {
#ifdef USE_GTK
   const char *room = gtk_chat_current_room();
   if (room && *room) return room;
#endif
   return ws_authoritative_room();
}
