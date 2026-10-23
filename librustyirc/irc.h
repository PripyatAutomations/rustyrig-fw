#ifndef LIBRUSTYIRC_IRC_H
#define LIBRUSTYIRC_IRC_H
#include <stddef.h>
#include <stdbool.h>
/* IRC line without CRLF, with command in argv[0]. Trailing parameter
 * retains spaces; no application connection or protocol dependencies. */
#define RUSTYIRC_MAX_PARAMS 15
#define RUSTYIRC_MAX_LINE 510
typedef struct {
   char *prefix;
   size_t argc;
   char *argv[1 + RUSTYIRC_MAX_PARAMS + 1];
} rustyirc_message_t;
rustyirc_message_t *rustyirc_parse(const char *line);
void rustyirc_message_free(rustyirc_message_t *message);
/* RPL_WHOISUSER 311 has recipient, target, username, hostname,
 * literal '*', then trailing real name. Returns false if malformed. */
bool rustyirc_whoisuser(const rustyirc_message_t *message,
                       const char **nick, const char **user,
                       const char **host, const char **realname);
#endif
