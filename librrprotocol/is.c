// is.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/core.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include <time.h>
//#include "../ext/libmongoose/mongoose.h"
#include <librrprotocol/rrprotocol.h>
// is an admin or owner online?
bool is_admin_online(void) {
   if (http_client_list == NULL) {
      return false;
   }

   http_client_t *curr = http_client_list;
   while (curr) {
      if (!curr->is_ws || !curr->authenticated || curr->user == NULL) {
         return false;
      }
      if (has_priv(curr->user->uid, "admin|owner")) {
         return true;
      }
      curr = curr->next;
   }
   return false;
}

// is an elmer online?
bool is_elmer_online(void) {
   if (http_client_list == NULL) {
      return false;
   }

   http_client_t *curr = http_client_list;
   while (curr) {
      if (!curr->is_ws || !curr->authenticated || !curr->user) {
         continue;
      }
      if (client_has_flag(curr, FLAG_ELMER)) {
         Log(LOG_CRAZY, "auth", "is_elmer_online: returning cptr:<%x> - |%s|", curr, curr->chatname);
         return true;
      }
      curr = curr->next;
   }
   return false;
}
