/* Standalone IRC line parser, extracted from librrprotocol/irc.parser.c.
 * No rrconn_t or RustyRig protocol dependencies. */
#include "irc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static char *copy_range(const char *start, size_t length) {
   char *result = malloc(length + 1);
   if (!result) return NULL;
   memcpy(result, start, length);
   result[length] = 0;
   return result;
}
void rustyirc_message_free(rustyirc_message_t *m) {
   if (!m) return;
   free(m->prefix);
   for (size_t i = 0; i < m->argc; ++i) free(m->argv[i]);
   free(m);
}
rustyirc_message_t *rustyirc_parse(const char *line) {
   if (!line) return NULL;
   size_t len = strlen(line);
   if (!len || len > RUSTYIRC_MAX_LINE || strchr(line, '\r') || strchr(line, '\n')) return NULL;
   rustyirc_message_t *m = calloc(1, sizeof(*m));
   if (!m) return NULL;
   const char *p = line;
   if (*p == ':') {
      const char *begin = ++p;
      while (*p && *p != ' ') ++p;
      if (p == begin || !*p) goto invalid;
      m->prefix = copy_range(begin, (size_t)(p - begin));
      if (!m->prefix) goto invalid;
   }
   while (*p == ' ') ++p;
   if (!*p || *p == ':') goto invalid;
   while (*p) {
      if (m->argc >= 1 + RUSTYIRC_MAX_PARAMS) goto invalid;
      if (*p == ':') {
         if (!m->argc) goto invalid;
         m->argv[m->argc++] = copy_range(p + 1, strlen(p + 1));
         if (!m->argv[m->argc - 1]) goto invalid;
         break;
      }
      const char *begin = p;
      while (*p && *p != ' ') ++p;
      m->argv[m->argc++] = copy_range(begin, (size_t)(p - begin));
      if (!m->argv[m->argc - 1]) goto invalid;
      while (*p == ' ') ++p;
   }
   if (!m->argc) goto invalid;
   return m;
invalid:
   rustyirc_message_free(m);
   return NULL;
}
bool rustyirc_whoisuser(const rustyirc_message_t *m,
                       const char **nick, const char **user,
                       const char **host, const char **realname) {
   if (!m || m->argc != 7 || strcmp(m->argv[0], "311") ||
       strcmp(m->argv[5], "*")) return false;
   if (nick) *nick = m->argv[2];
   if (user) *user = m->argv[3];
   if (host) *host = m->argv[4];
   if (realname) *realname = m->argv[6];
   return true;
}
