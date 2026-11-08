// Client-side wire room compatibility state. Server-owned chat policy stays in rrserver.
// SPDX-License-Identifier: GPL-2.0-only
#include "build_config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/wire.h>
#include <librustyaxe/transport.h>

static char authoritative_override[128];
static uint32_t authoritative_mask = 3;

bool ws_room_name_valid(const char *room) {
   if (!room || (room[0] != '#' && room[0] != '&') || !room[1] || strlen(room) >= 128) return false;
   for (const unsigned char *p = (const unsigned char *)room; *p; p++)
      if (isspace(*p) || *p == ',' || *p < 32 || *p == 127) return false;
   return true;
}

bool ws_room_rig_base(const char *room) {
   if (!ws_room_name_valid(room)) return false;
   const char *dash = strrchr(room, '-');
   if (!dash || strncasecmp(dash, "-rig", 4) || !isdigit((unsigned char)dash[4])) return false;
   const char *p = dash + 4;
   while (isdigit((unsigned char)*p)) p++;
   return !*p;
}

bool ws_room_same_rig(const char *room, const char *base) {
   if (!room || !base || !ws_room_rig_base(base)) return false;
   size_t n = strlen(base);
   return !strncasecmp(room, base, n) && (!room[n] || (room[n] == '.' && room[n+1]));
}

const char *ws_authoritative_room(void) {
   if (authoritative_override[0]) return authoritative_override;
   static char room[128];
   char *configured = (char *)cfg_get_exp("station.name");
   snprintf(room, sizeof(room), "#%s-rig0", configured && *configured ? configured : "rustyrig");
   free(configured);
   return room;
}

void ws_set_authoritative_room(const char *room) {
   if (ws_room_name_valid(room)) snprintf(authoritative_override, sizeof(authoritative_override), "%s", room);
}

void ws_set_authoritative_vfo_mask(uint32_t mask) { authoritative_mask = mask; }

typedef struct {
   char room[128];
   uint32_t mask;
} client_wire_rooms;

void ws_rooms_context_swap(void **saved) {
   if (!saved) return;
   if (!*saved) {
      client_wire_rooms *new_state = calloc(1, sizeof(*new_state));
      if (!new_state) abort();
      new_state->mask = 3;
      *saved = new_state;
   }
   client_wire_rooms *state = *saved;
   char temp[sizeof(authoritative_override)];
   memcpy(temp, authoritative_override, sizeof(temp));
   memcpy(authoritative_override, state->room, sizeof(temp));
   memcpy(state->room, temp, sizeof(temp));
   uint32_t old = authoritative_mask;
   authoritative_mask = state->mask;
   state->mask = old;
}

/* Client outbound messages use the existing wire codec and selected transport. */
bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int opcode) {
   (void)sender;
   if (!dest || !d || opcode != WEBSOCKET_OP_TEXT ||
       !dest->native_transport || rr_transport_state(dest->native_transport) != RR_TRANSPORT_OPEN)
      return false;
   const char *encoded = rr_wire_encode(d);
   if (!encoded) return false;
   int status = rr_transport_send_message(dest->native_transport, encoded, strlen(encoded));
   free((void *)encoded);
   return status == RR_TRANSPORT_OK;
}
