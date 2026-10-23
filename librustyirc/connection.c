#include "connection.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
struct rustyirc_connection {
   rustyirc_callbacks_t callbacks;
   char nick[64];
   char line[RUSTYIRC_MAX_LINE + 3];
   size_t buffered;
   bool registered, dropping;
};
rustyirc_connection_t *rustyirc_connection_new(const rustyirc_callbacks_t *callbacks) {
   if (!callbacks) return NULL;
   rustyirc_connection_t *c = calloc(1, sizeof(*c));
   if (c) c->callbacks = *callbacks;
   return c;
}
void rustyirc_connection_free(rustyirc_connection_t *c) { free(c); }
bool rustyirc_connection_set_nick(rustyirc_connection_t *c, const char *nick) {
   if (!c || !nick || !*nick || strlen(nick) >= sizeof(c->nick)) return false;
   for (const unsigned char *p = (const unsigned char *)nick; *p; ++p)
      if (*p <= ' ' || *p == ':' || *p == ',' || *p == '#') return false;
   strcpy(c->nick, nick);
   return true;
}
const char *rustyirc_connection_nick(const rustyirc_connection_t *c) { return c ? c->nick : ""; }
bool rustyirc_connection_registered(const rustyirc_connection_t *c) { return c && c->registered; }
void rustyirc_connection_reset(rustyirc_connection_t *c) {
   if (!c) return;
   c->buffered = 0;
   c->registered = false;
   c->dropping = false;
}
bool rustyirc_connection_send(rustyirc_connection_t *c, const char *line) {
   if (!c || !c->callbacks.send || !line) return false;
   size_t n = strlen(line);
   if (!n || n > RUSTYIRC_MAX_LINE || strchr(line, '\r') || strchr(line, '\n')) return false;
   char wire[RUSTYIRC_MAX_LINE + 3];
   memcpy(wire, line, n);
   wire[n] = '\r'; wire[n + 1] = '\n';
   return c->callbacks.send(c->callbacks.context, wire, n + 2);
}
static void deliver(rustyirc_connection_t *c) {
   c->line[c->buffered] = 0;
   if (c->buffered && c->line[c->buffered - 1] == '\r')
      c->line[--c->buffered] = 0;
   rustyirc_message_t *m = rustyirc_parse(c->line);
   c->buffered = 0;
   if (!m) return;
   if (!strcmp(m->argv[0], "001")) c->registered = true;
   if (!strcmp(m->argv[0], "NICK") && m->argc > 1 && m->prefix && *c->nick) {
      size_t nicklen = strcspn(m->prefix, "!");
      if (strlen(c->nick) == nicklen && !strncasecmp(m->prefix, c->nick, nicklen))
         rustyirc_connection_set_nick(c, m->argv[1]);
   }
   /* Never deliver DCC AUDIO grants to ordinary chat callbacks, including
    * malformed or misaddressed offers: payloads can contain credentials. */
   bool audio = (!strcmp(m->argv[0], "PRIVMSG") ||
                 !strcmp(m->argv[0], "NOTICE")) &&
                m->argc >= 3 && m->argv[2] &&
                !strncmp(m->argv[2], "\001DCC AUDIO", 10);
   if (audio) {
      if (m->prefix && m->argv[1] && *c->nick &&
          !strcasecmp(m->argv[1], c->nick) && c->callbacks.on_dcc_audio)
         c->callbacks.on_dcc_audio(c->callbacks.context, c, m, m->argv[2]);
   } else if (c->callbacks.on_message) {
      c->callbacks.on_message(c->callbacks.context, c, m);
   }
   rustyirc_message_free(m);
}
bool rustyirc_connection_feed(rustyirc_connection_t *c, const void *bytes, size_t size) {
   if (!c || (!bytes && size)) return false;
   const unsigned char *p = bytes;
   bool valid = true;
   for (size_t i = 0; i < size; ++i) {
      if (p[i] == '\n') {
         if (!c->dropping) deliver(c);
         c->dropping = false;
         c->buffered = 0;
      } else if (c->dropping) {
         continue;
      } else if (c->buffered < sizeof(c->line) - 1) {
         c->line[c->buffered++] = (char)p[i];
      } else {
         /* Discard an oversized line until its terminating LF. */
         c->buffered = 0;
         c->dropping = true;
         valid = false;
      }
   }
   return valid;
}
