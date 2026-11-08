//
// srv.chat.whois.c: Account room membership for WHOIS
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/srv.chat.internal.h>

/* WHOIS is account-scoped: union the joined rooms of authenticated sessions. Use the membership strings rather than the optional room metadata cache. PARITY:
 * rrclient/events.c and rustyrig-www/js/webui.chat.js WHOIS Rooms. */
char *ws_chat_whois_rooms(const rrconn_t *target) {
   const size_t capacity = HTTP_MAX_SESSIONS * AUTOJOIN_LEN + 1;
   char *rooms = calloc(capacity, 1);
   dict *seen = dict_new();

   if (!rooms || !seen) {
      free(rooms);
      dict_free(seen);

      return NULL;
   }

   for (rrconn_t *session = http_client_list ; session ; session = session->next) {
      if (!session->authenticated || !session->user || session->user->uid != target->user->uid) {
         continue;
      }
      char copy[AUTOJOIN_LEN];
      strlcpy(copy, session->rooms, sizeof(copy));
      char *save = NULL;

      for (char *room = strtok_r(copy, ",", &save) ; room ; room = strtok_r(NULL, ",", &save)) {
         char folded[128];

         if (!ws_room_name_valid(room)) {
            continue;
         }
         strlcpy(folded, room, sizeof(folded));

         for (char *p = folded ; *p ; p++) {
            *p = tolower((unsigned char)*p);
         }

         if (dict_get(seen, folded, NULL)) {
            continue;
         }

         if (dict_add(seen, folded, "1") < 0) {
            free(rooms);
            dict_free(seen);

            return NULL;
         }

         if ((rooms[0] && strlcat(rooms, " ", capacity) >= capacity) || strlcat(rooms, room, capacity) >= capacity) {
            free(rooms);
            dict_free(seen);

            return NULL;
         }
      }
   }

   dict_free(seen);

   return rooms;
}
