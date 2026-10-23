//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
rustyirc_connection_t *rustyirc_connection_new(int fd, const rustyirc_callbacks_t *callbacks) {
   if (!callbacks) {
      return NULL;
   }
   rustyirc_connection_t *c = calloc(1, sizeof(*c));

   if (c) {
      c->callbacks = *callbacks;
      c->fd = fd;
      c->references = 1;
   }

   if (c && (!rustyirc_register_default_callbacks() || !rustyirc_register_default_numeric_callbacks())) {
      free(c);

      return NULL;
   }

   return c;
}

/* Transport failures may release the application's ownership synchronously from send/event callbacks. Keep protocol frames alive until their outer call
 * returns. */
bool rustyirc_connection_hold(irc_conn_t *c) {
   if (!c || c->closing) {
      return false;
   }
   ++c->references;

   return true;
}
void rustyirc_connection_release(irc_conn_t *c) {
   if (--c->references) {
      return;
   }
   rustyirc_capabilities_clear(c);
   volatile unsigned char *secret = (volatile unsigned char *)&c->credentials;

   for (size_t i = 0 ; i < sizeof(c->credentials) ; ++i) {
      secret[i] = 0;
   }

   free(c);
}
void rustyirc_connection_free(irc_conn_t *c) {
   if (!c || c->closing) {
      return;
   }
   c->closing = true;
   rustyirc_connection_release(c);
}
bool rustyirc_connection_set_nick(rustyirc_connection_t *c, const char *nick) {
   if (!c || !nick || !*nick || strlen(nick) >= sizeof(c->nick)) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)nick ; *p ; ++p) {
      if (*p <= ' ' || *p == ':' || *p == ',' || *p == '#') {
         return false;
      }
   }

   strcpy(c->nick, nick);

   return true;
}
const char *rustyirc_connection_nick(const rustyirc_connection_t *c) {
   return c ? c->nick : "";
}
bool rustyirc_connection_registered(const rustyirc_connection_t *c) {
   return c && c->registered;
}
void rustyirc_connection_reset(rustyirc_connection_t *c) {
   if (!c) {
      return;
   }
   rustyirc_capabilities_clear(c);
   c->recvq[0] = 0;
   c->registered = false;
   c->sent_login = false;
}
static bool rustyirc_connection_send_impl(rustyirc_connection_t *c, const char *line) {
   if (!c || !c->callbacks.send || !line) {
      return false;
   }
   size_t n = strlen(line);

   if (!n || n > rustyirc_line_limit(c) - 2 || strchr(line, '\r') || strchr(line, '\n')) {
      return false;
   }
   char wire[RUSTYIRC_MAX_LINE + 3];
   memcpy(wire, line, n);
   wire[n] = '\r';
   wire[n + 1] = '\n';

   return c->callbacks.send(c->callbacks.context, wire, n + 2);
}
void rustyirc_connection_set_fd(irc_conn_t *c, int fd) {
   if (c) {
      c->fd = fd;
   }
}
int rustyirc_connection_fd(const irc_conn_t *c) {
   return c ? c->fd : -1;
}
void rustyirc_connection_set_sasl(irc_conn_t *c, const char *user, const char *pass, const char *mechanism) {
   if (!c) {
      return;
   }
   snprintf(c->credentials.sasl_user, sizeof(c->credentials.sasl_user), "%s", user ? user : "");
   snprintf(c->credentials.sasl_pass, sizeof(c->credentials.sasl_pass), "%s", pass ? pass : "");
   snprintf(c->credentials.sasl_mechanism, sizeof(c->credentials.sasl_mechanism), "%s", mechanism ? mechanism : "");
}
bool rustyirc_send(irc_conn_t *c, const char *fmt, ...) {
   char line[IRC_MSGLEN];
   va_list args;
   va_start(args, fmt);
   int n = vsnprintf(line, sizeof(line), fmt, args);
   va_end(args);

   return n >= 0 && (size_t)n <= rustyirc_line_limit(c) - 2 && rustyirc_connection_send(c, line);
}
void rustyirc_event_emit(const char *event, irc_conn_t *c, const char *data) {
   if (c && !c->closing && c->callbacks.on_event) {
      c->callbacks.on_event(c->callbacks.context, c, event, data);
   }
}
void rustyirc_event_emit_dict(const char *event, irc_conn_t *c, dict *d) {
   char *json = dict2json(d);

   if (json) {
      rustyirc_event_emit(event, c, json);
   }
   free(json);
}

static bool rustyirc_connection_feed_impl(irc_conn_t *cptr, const void *data, size_t n) {
   if (!cptr || (!data && n)) {
      return false;
   }
   const unsigned char *bytes = data;
   size_t buffered = strlen(cptr->recvq);

   /* Process complete CRLF-delimited lines as data arrives, rather than rejecting a large network read containing several valid messages. Incomplete lines
    * remain in recvq across calls. */
   for (size_t i = 0 ; i < n ; ++i) {
      unsigned char ch = bytes[i];

      if (!ch) {
         return false;
      }

      if (buffered >= IRC_MSGLEN) {
         Log(LOG_WARN, "irc", "IRC line exceeds configured limit");

         return false;
      }
      cptr->recvq[buffered++] = (char)ch;
      cptr->recvq[buffered] = '\0';

      if (ch != '\n') {
         continue;
      }

      if (buffered < 2 || cptr->recvq[buffered - 2] != '\r') {
         Log(LOG_WARN, "irc", "IRC line missing CRLF terminator");

         return false;
      }

      if (buffered - 2 > IRC_MSGLEN - 2) {
         return false;
      }
      cptr->recvq[buffered - 2] = '\0';

      if (strcasestr(cptr->recvq, "DCC AUDIO")) {
         Log(LOG_DEBUG, "net", "processing DCC AUDIO referral (credentials omitted)");
      } else {
         Log(LOG_DEBUG, "net", "processing line: [%s]", cptr->recvq);
      }
      rustyirc_process_message(cptr, cptr->recvq);

      if (cptr->closing) {
         return false;
      }
      buffered = 0;
      cptr->recvq[0] = '\0';
   }

   return true;
}

static bool rustyirc_connection_register_impl(irc_conn_t *cptr, const char *ident, const char *account, const char *pass) {
   if (!cptr) {
      return false;
   }

   if (!cptr->sent_login) {
      ident = ident && *ident ? ident : cptr->nick;
      account = account ? account : "";
      pass = pass ? pass : "";

      if (!cptr->nick[0] || strpbrk(cptr->nick, " :\t\r\n") || strpbrk(ident, " :\t\r\n")) {

         return false;
      }
      bool sent = true;

      if (pass[0]) {
         if (account[0]) {
            sent = rustyirc_send(cptr, "PASS %s:%s", account, pass);
         } else {
            sent = rustyirc_send(cptr, "PASS %s", pass);
         }
      }
      rustyirc_capabilities_reset(cptr);
      sent = sent && rustyirc_send(cptr, "CAP LS 302") && rustyirc_send(cptr, "NICK %s", cptr->nick)
         && rustyirc_send(cptr, "USER %s 0 * :%s", ident, cptr->nick);

      if (!sent) {

         return false;
      }
      cptr->sent_login = true;
   }

   return true;
}


bool rustyirc_connection_send(irc_conn_t *c, const char *line) {
   if (!rustyirc_connection_hold(c)) {
      return false;
   }
   bool result = rustyirc_connection_send_impl(c, line);
   rustyirc_connection_release(c);

   return result;
}

bool rustyirc_connection_feed(irc_conn_t *c, const void *bytes, size_t size) {
   if (!rustyirc_connection_hold(c)) {
      return false;
   }
   bool result = rustyirc_connection_feed_impl(c, bytes, size);
   rustyirc_connection_release(c);

   return result;
}

bool rustyirc_connection_register(irc_conn_t *c, const char *ident, const char *account, const char *pass) {
   if (!rustyirc_connection_hold(c)) {
      return false;
   }
   bool result = rustyirc_connection_register_impl(c, ident, account, pass);
   rustyirc_connection_release(c);

   return result;
}
