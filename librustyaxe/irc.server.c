//
// Stuff strictly related to IRC server
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <netdb.h>
#include <librustyaxe/core.h>
#include <ev.h>

// Recompose IRC message from irc_message_t
void irc_build_message(const irc_message_t *mp, char *msg, size_t msglen) {
   size_t pos = 0;

   if (!mp || !msg || msglen == 0) {
      return;
   }

   // Prefix
   if (mp->prefix) {
      pos += snprintf(msg + pos, msglen - pos, ":%s ", mp->prefix);
   }

   for (int i = 0; i < mp->argc; i++) {
      const char *arg = mp->argv[i] ? mp->argv[i] : "";

      // Last argument → prefix with ':'
      if (i == mp->argc - 1 && (strchr(arg, ' ') || arg[0] == ':')) {
         pos += snprintf(msg + pos, msglen - pos, ":%s", arg);
      } else {
         pos += snprintf(msg + pos, msglen - pos, "%s%s",
                         (i > 0 ? " " : ""), arg);
      }

      if (pos >= msglen) {
         msg[msglen - 1] = '\0';
         return;
      }
   }
}

bool irc_sendto_all(rrlist_t *conn_list, irc_client_t *cptr, irc_message_t *mp) {
   if (!mp) {
      // This message isn't valid
      return true;
   }

   // this rarely will match, because the current connection will be in the list...
   if (!conn_list) {
      // if no other connections, bail out early
      return false;
   }

   rrlist_t *lptr = conn_list;
   // Compose the message
   char msg[IRC_MSGLEN + 1];
   memset(msg, 0, IRC_MSGLEN + 1);
   irc_build_message(mp, msg, sizeof(msg));
   Log(LOG_DEBUG, "irc", "Rebuilt message: %s", msg);

   // Walk the list
   while (lptr) {
      irc_client_t *acptr = NULL;
      if (lptr->ptr) {
         acptr = (irc_client_t *)lptr->ptr;
      }

      // skip cptr
      if (cptr && acptr == cptr) {
         lptr = lptr->next;
         continue;
      }

      if (acptr && acptr->is_server && acptr->fd) {
         // Send to the client
         irc_send(acptr, "%s", msg);
      }

      lptr = lptr->next;
   }
   return false;
}
