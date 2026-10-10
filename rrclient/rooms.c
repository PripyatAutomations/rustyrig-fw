// rrclient/rooms.c: Handling of chat/control rooms
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/rooms.h>
#include <rrclient/connman.h>
#include <rrclient/frontend.h>

extern rrconn_t *ws_conn;

typedef struct client_room {
   char name[128];
   char vfos[512];
   char topic[512];
   uint32_t vfo_mask;
   bool tx_control;
   bool rx_tunable;
   uint32_t rx_tuning_mask;
   char active_vfo;
   struct client_room *next;
} client_room_t;

static client_room_t *rooms;
static char **available_rooms;
static size_t available_room_count;
static char **reconnect_rooms;
static size_t reconnect_room_count;

static void reconnect_rooms_clear(void) {
   for (size_t i = 0 ; i < reconnect_room_count ; i++) {
      free(reconnect_rooms[i]);
   }

   free(reconnect_rooms);
   reconnect_rooms = NULL;
   reconnect_room_count = 0;
}

static void joined_rooms_clear(void) {
   while (rooms) {
      client_room_t *next = rooms->next;
      free(rooms);
      rooms = next;
   }
}

static bool room_names_contain(char *const *list, size_t count, const char *room) {
   for (size_t i = 0 ; i < count ; i++) {
      if (!strcasecmp(list[i], room)) {
         return true;
      }
   }

   return false;
}

static void available_rooms_clear(void) {
   for (size_t i = 0 ; i < available_room_count ; i++) {
      free(available_rooms[i]);
   }

   free(available_rooms);
   available_rooms = NULL;
   available_room_count = 0;
}

static const char *canonical(const char *room) {
   return (room && *room) ? room : ws_authoritative_room();
}

bool rrclient_room_is_joined(const char *room) {
   const char *want = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, want) ) {
         return true;
      }
   }

   return false;
}

bool rrclient_room_join(const char *room) {
   const char *name = canonical(room);

   if (!name || !*name || rrclient_room_is_joined(name) ) {
      return true;
   }
   client_room_t *r = calloc(1, sizeof(*r) );

   if (!r) {
      return false;
   }
   strlcpy(r->name, name, sizeof(r->name) );
   r->active_vfo = 'A';
   r->next = rooms;
   rooms = r;

   return true;
}

bool rrclient_room_request_join(const char *room) {
   if (!room || !*room || !ws_conn || rrclient_room_is_joined(room) ) {
      return false;
   }
   dict *d = dict_new();

   if (!d) {
      return false;
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "join");
   dict_add(d, "talk.target", room);
   bool ok = ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return ok;
}

bool rrclient_room_part(const char *room) {
   const char *name = canonical(room);

   if (!name || !*name) {
      return false;
   }
   bool removed = false;

   for (size_t i = 0 ; i < reconnect_room_count ; ) {
      if (!strcasecmp(reconnect_rooms[i], name)) {
         free(reconnect_rooms[i]);
         memmove(reconnect_rooms + i, reconnect_rooms + i + 1, (reconnect_room_count - i - 1) * sizeof(*reconnect_rooms));
         reconnect_room_count--;
         removed = true;
      } else {
         i++;
      }
   }

   client_room_t **pp = &rooms;
   while (*pp) {
      client_room_t *r = *pp;

      if (!strcasecmp(r->name, name) ) {
         *pp = r->next;
         free(r);

         return true;
      }
      pp = &r->next;
   }
   return removed;
}

void rrclient_rooms_clear(void) {
   joined_rooms_clear();
   available_rooms_clear();
   reconnect_rooms_clear();
}

void rrclient_rooms_disconnect(void) {
   if (rooms) {
      reconnect_rooms_clear();

      for (client_room_t *room = rooms ; room ; room = room->next) {
         if (room->name[0] != '#' && room->name[0] != '&') {
            continue;
         }
         char **grown = realloc(reconnect_rooms, (reconnect_room_count + 1) * sizeof(*grown));

         if (!grown) {
            break;
         }
         reconnect_rooms = grown;
         reconnect_rooms[reconnect_room_count] = strdup(room->name);

         if (reconnect_rooms[reconnect_room_count]) {
            reconnect_room_count++;
         }
      }
   }
   joined_rooms_clear();
   available_rooms_clear();
}

void rrclient_rooms_rejoin_available(void) {
   for (size_t i = 0 ; i < reconnect_room_count ; i++) {
      if ((ws_conn && ws_conn->server && !ws_conn->is_ws) ||
         room_names_contain(available_rooms, available_room_count, reconnect_rooms[i])) {
         rrclient_room_request_join(reconnect_rooms[i]);
      }
   }

   reconnect_rooms_clear();
}

const char *rrclient_room_iter(unsigned int index) {
   client_room_t *room = rooms;
   while (room && index--) {
      room = room->next;
   }
   return room ? room->name : NULL;
}

void rrclient_rooms_set_available(const char *list) {
   available_rooms_clear();
   char *copy = list ? strdup(list) : NULL;

   if (!copy) {
      return;
   }
   char *save = NULL;

   for (char *room = strtok_r(copy, " \t\r\n", &save) ; room ; room = strtok_r(NULL, " \t\r\n", &save)) {
      if (available_room_count >= 1024 || (room[0] != '#' && room[0] != '&')) {
         continue;
      }
      bool duplicate = false;

      for (size_t i = 0 ; i < available_room_count ; i++) {
         if (!strcasecmp(available_rooms[i], room)) {
            duplicate = true;
            break;
         }
      }

      if (duplicate) {
         continue;
      }
      char **grown = realloc(available_rooms, (available_room_count + 1) * sizeof(*grown));

      if (!grown) {
         break;
      }
      available_rooms = grown;
      available_rooms[available_room_count] = strdup(room);

      if (available_rooms[available_room_count]) {
         available_room_count++;
      }
   }

   free(copy);
}

void rrclient_room_available_remove(const char *room) {
   if (!room) {
      return;
   }

   for (size_t i = 0 ; i < reconnect_room_count ; ) {
      if (strcasecmp(reconnect_rooms[i], room)) {
         i++;
         continue;
      }
      free(reconnect_rooms[i]);
      memmove(&reconnect_rooms[i], &reconnect_rooms[i + 1], (reconnect_room_count - i - 1) * sizeof(*reconnect_rooms));
      reconnect_room_count--;
   }

   for (size_t i = 0 ; i < available_room_count ; ) {
      if (strcasecmp(available_rooms[i], room)) {
         i++;
         continue;
      }
      free(available_rooms[i]);
      memmove(&available_rooms[i], &available_rooms[i + 1], (available_room_count - i - 1) * sizeof(*available_rooms));
      available_room_count--;
   }
}

const char *rrclient_room_available_iter(unsigned int index) {
   return index < available_room_count ? available_rooms[index] : NULL;
}

bool rrclient_room_set_vfos(const char *room, const char *vfos) {
   if (!rrclient_room_join(room) ) {
      return false;
   }
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         strlcpy(r->vfos, vfos ? vfos : "", sizeof(r->vfos) );

         return true;
      }
   }

   return false;
}

bool rrclient_room_set_vfo_mask(const char *room, unsigned long mask) {
   char vfos[256] = "";
   size_t used = 0;

   for (unsigned int i = 0 ; i < 32 ; i++) {
      if (!(mask & (1UL << i) ) ) {
         continue;
      }
      int n = snprintf(vfos + used, sizeof(vfos) - used, "%srig0.vfo_%c", used ? " " : "", (char)('a' + i) );

      if (n < 0 || (size_t)n >= sizeof(vfos) - used) {
         break;
      }
      used += (size_t)n;
   }

   bool ok = rrclient_room_set_vfos(room, vfos);
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         r->vfo_mask = (uint32_t)mask;
         break;
      }
   }

   return ok;
}

const char *rrclient_room_vfos(const char *room) {
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         return r->vfos;
      }
   }

   return "";
}

bool rrclient_room_set_topic(const char *room, const char *topic) {
   if (!rrclient_room_join(room) ) {
      return false;
   }
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         strlcpy(r->topic, topic ? topic : "", sizeof(r->topic) );

         return true;
      }
   }

   return false;
}

const char *rrclient_room_topic(const char *room) {
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         return r->topic;
      }
   }

   return "";
}

const char *rrclient_current_room(void) {
   if (!rrclient_present_context()) {
      return ws_authoritative_room();
   }

   if (frontend_ops() ) {
      const char *room = frontend_ops()->chat_current_room();

      if (room && *room) {
         return room;
      }
   }

   return ws_authoritative_room();
}

void rrclient_room_set_control_flags(const char *room, bool tx, bool rx) {
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         r->tx_control = tx;
         r->rx_tunable = rx;

         return;
      }
   }
}
bool rrclient_room_tx_control(const char *room) {
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         return r->tx_control;
      }
   }

   return false;
}
bool rrclient_room_rx_tunable(const char *room) {
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         return r->rx_tunable;
      }
   }

   return false;
}

void rrclient_room_set_rx_tuning_mask(const char *room, uint32_t mask) {
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         r->rx_tuning_mask = mask;

         return;
      }
   }
}
uint32_t rrclient_room_rx_tuning_mask(const char *room) {
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         return r->rx_tuning_mask;
      }
   }

   return 0;
}

uint32_t rrclient_room_vfo_mask(const char *room) {
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         return r->vfo_mask;
      }
   }

   return 0;
}

char rrclient_room_active_vfo(const char *room) {
   const char *name = canonical(room);

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) && r->active_vfo >= 'A' && r->active_vfo <= 'Z') {
         return r->active_vfo;
      }
   }

   return 'A';
}

void rrclient_room_set_active_vfo(const char *room, char vfo) {
   const char *name = canonical(room);

   if (vfo < 'A' || vfo > 'Z') {
      return;
   }

   for (client_room_t *r = rooms ; r ; r = r->next) {
      if (!strcasecmp(r->name, name) ) {
         r->active_vfo = vfo;

         return;
      }
   }
}

/* Save/load the component view when entering a server connection context. */
typedef struct {
   client_room_t *rooms;
   char **available_rooms, **reconnect_rooms;
   size_t available_room_count, reconnect_room_count;
} rrclient_rooms_context;
void rrclient_rooms_context_swap(void **saved) {
   if (!*saved) {
      *saved = calloc(1, sizeof(rrclient_rooms_context));

      if (!*saved) {
         abort();
      }
   }
   rrclient_rooms_context *state = *saved;
   {
      __typeof__(rooms) temporary;
      memcpy(&temporary, &rooms, sizeof(rooms));
      memcpy(&rooms, &state->rooms, sizeof(rooms));
      memcpy(&state->rooms, &temporary, sizeof(rooms));
   }
   {
      __typeof__(available_rooms) temporary;
      memcpy(&temporary, &available_rooms, sizeof(available_rooms));
      memcpy(&available_rooms, &state->available_rooms, sizeof(available_rooms));
      memcpy(&state->available_rooms, &temporary, sizeof(available_rooms));
   }
   {
      __typeof__(available_room_count) temporary;
      memcpy(&temporary, &available_room_count, sizeof(available_room_count));
      memcpy(&available_room_count, &state->available_room_count, sizeof(available_room_count));
      memcpy(&state->available_room_count, &temporary, sizeof(available_room_count));
   }
   {
      __typeof__(reconnect_rooms) temporary;
      memcpy(&temporary, &reconnect_rooms, sizeof(reconnect_rooms));
      memcpy(&reconnect_rooms, &state->reconnect_rooms, sizeof(reconnect_rooms));
      memcpy(&state->reconnect_rooms, &temporary, sizeof(reconnect_rooms));
   }
   {
      __typeof__(reconnect_room_count) temporary;
      memcpy(&temporary, &reconnect_room_count, sizeof(reconnect_room_count));
      memcpy(&reconnect_room_count, &state->reconnect_room_count, sizeof(reconnect_room_count));
      memcpy(&state->reconnect_room_count, &temporary, sizeof(reconnect_room_count));
   }
}

void rrclient_rooms_context_free(void *saved) {
   if (!saved) {
      return;
   }
   rrclient_rooms_context_swap(&saved);
   rrclient_rooms_clear();
   rrclient_rooms_context_swap(&saved);
   free(saved);
}
