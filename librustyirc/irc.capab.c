#include "internal.h"
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

const irc_cap_t irc_capabilities[] = {
   {
      "multi-prefix", "Server may send multiple nick prefixes (@+nick)"
   },
   {
      "RUSTYRIG", "RustyRig extensions"
   },
   {
      "sasl", "SASL authentication (PLAIN, EXTERNAL, etc.)"
   },
   {
      "account-notify", "Notify when a user logs in/out of services"
   },
   {
      "extended-join", "JOIN messages include account name and realname"
   },
   {
      "away-notify", "Away/back status notifications"
   },
   {
      "chghost", "Servers can change visible hostname"
   },
   {
      "userhost-in-names", "NAMES list includes user@host"
   },
   {
      "message-tags", "Arbitrary key=value tags on messages"
   },
   {
      "echo-message", "Client sees its own PRIVMSG/NOTICE echoes"
   },
   {
      "server-time", "Messages include server-time tag"
   },

   // Older CAPABs from Unreal/Hybrid/Chary/etc.
   {
      "TS", "TimeStamp protocol"
   },
   {
      "QS", "Quit storm protection"
   },
   {
      "EX", "Channel ban exceptions"
   },
   {
      "CHW", "Channel wallops"
   },
   {
      "IE", "Invite exceptions"
   },

   {
      NULL, NULL
   }
};

typedef struct irc_cap_state {
   const irc_conn_t *connection;
   bool rusty, negotiating;
   char modes[32], symbols[32], chanmodes[128];
   char display[32][2];
   char requests[64];
   dict *properties, *capabilities, *enabled, *capabs;
   bool listing, listing_enabled, sasl_wait, sasl_sent, sasl_requested;
   long long sasl_deadline;
   struct irc_cap_state *next;
} irc_cap_state_t;
static irc_cap_state_t *states;

static irc_cap_state_t *state(const irc_conn_t *c, bool create) {
   for (irc_cap_state_t *s = states ; s ; s = s->next) {
      if (s->connection == c) {
         return s;
      }
   }

   if (!c || !create) {
      return NULL;
   }
   irc_cap_state_t *s = calloc(1, sizeof(*s));

   if (s) {
      s->properties = dict_new();

      s->capabilities = dict_new();
      s->enabled = dict_new();
      s->capabs = dict_new();

      if (!s->properties || !s->capabilities || !s->enabled || !s->capabs) {
         dict_free(s->properties);
         dict_free(s->capabilities);
         dict_free(s->enabled);
         dict_free(s->capabs);
         free(s);

         return NULL;
      }
      s->connection = c;
      strcpy(s->modes, "qaohv");
      strcpy(s->symbols, "~&@%+");

      for (size_t i = 0 ; s->symbols[i] ; i++) {
         s->display[i][0] = s->symbols[i];
      }

      strcpy(s->chanmodes, "b,k,l,imnpst");
      s->next = states;
      states = s;
   }

   return s;
}

void rustyirc_capabilities_clear(irc_conn_t *c) {
   for (irc_cap_state_t **p = &states ; *p ; p = &(*p)->next) {
      if ((*p)->connection == c) {
         irc_cap_state_t *s = *p;
         *p = s->next;
         dict_free(s->properties);
         dict_free(s->capabilities);
         dict_free(s->enabled);
         dict_free(s->capabs);
         free(s);

         return;
      }
   }
}
void rustyirc_capabilities_shutdown(void) {
   while (states) {
      rustyirc_capabilities_clear((irc_conn_t *) states->connection);
   }
}
void rustyirc_capabilities_reset(irc_conn_t *c) {
   rustyirc_capabilities_clear(c);
   irc_cap_state_t *s = state(c, true);

   if (s) {
      s->negotiating = true;
   }
}

/* ISUPPORT values are connection-owned; NULL means absent, "" a flag. */
const char *rustyirc_server_property(const irc_conn_t *c, const char *name) {
   irc_cap_state_t *s = state(c, false);
   char key[IRC_MSGLEN + 1];

   if (!s || !name || strlen(name) >= sizeof(key)) {
      return NULL;
   }

   for (size_t i = 0 ;; i++) {
      key[i] = toupper((unsigned char)name[i]);

      if (!name[i]) {
         break;
      }
   }

   return dict_get(s->properties, key, NULL);
}

const char *rustyirc_server_capability(const irc_conn_t *c, const char *name) {
   irc_cap_state_t *s = state(c, false);

   return s && name ? dict_get(s->capabilities, name, NULL) : NULL;
}

bool rustyirc_capability_enabled(const irc_conn_t *c, const char *name) {
   irc_cap_state_t *s = state(c, false);

   return s && name && dict_get(s->enabled, name, NULL) != NULL;
}

const char *rustyirc_server_capab(const irc_conn_t *c, const char *name) {
   irc_cap_state_t *s = state(c, false);

   return s && name ? dict_get(s->capabs, name, NULL) : NULL;
}

size_t rustyirc_line_limit(const irc_conn_t *c) {
   const char *value = rustyirc_server_property(c, "LINELEN");

   if (!value || !*value) {
      return 512;
   }
   size_t limit = 0;

   for (const unsigned char *p = (const unsigned char *)value ; *p ; p++) {
      if (!isdigit(*p)) {
         return 512;
      }

      /* Keep the existing transport buffer bound, without numeric overflow. */
      if (limit < IRC_MSGLEN) {
         limit = limit * 10 + (*p - '0');

         if (limit > IRC_MSGLEN) {
            limit = IRC_MSGLEN;
         }
      }
   }

   return limit >= 2 ? limit : 512;
}

bool rustyirc_supports_rustyrig(const irc_conn_t *c) {
   irc_cap_state_t *s = state(c, false);

   return s && s->rusty;
}
char rustyirc_prefix_mode(const irc_conn_t *c, char symbol) {
   irc_cap_state_t *s = state(c, false);
   const char *symbols = s ? s->symbols : "~&@%+";
   const char *modes = s ? s->modes : "qaohv";
   const char *p = symbol ? strchr(symbols, symbol) : NULL;

   return p ? modes[p - symbols] : 0;
}
const char *rustyirc_modes_symbol(const irc_conn_t *c, const char *modes) {
   irc_cap_state_t *s = state(c, false);
   const char *order = s ? s->modes : "qaohv";
   /* Return stable single-character strings without global scratch state. */
   static const char *display[] = {
      "~", "&", "@", "%", "+"
   };

   for (size_t i = 0 ; order[i] ; i++) {
      if (strchr(modes, order[i])) {
         return s ? s->display[i] : display[i];
      }
   }

   return "";
}
bool rustyirc_mode_has_argument(const irc_conn_t *c, char mode, bool adding) {
   irc_cap_state_t *s = state(c, false);

   if (strchr(s ? s->modes : "qaohv", mode)) {
      return true;
   }
   unsigned group = 0;

   for (const char *p = s ? s->chanmodes : "b,k,l,imnpst" ; *p ; p++) {
      if (*p == ',') {
         group++;
      } else if (*p == mode) {
         return group < 2 || (group == 2 && adding);
      }
   }

   return false;
}
static bool sasl_configured(irc_conn_t *c) {
   return(c->credentials.sasl_user[0] || !strcasecmp(c->credentials.sasl_mechanism, "EXTERNAL"));
}
static void sasl_finish(irc_conn_t *c, irc_cap_state_t *s, const char *result) {
   s->sasl_wait = false;
   s->sasl_deadline = 0;

   if (s->negotiating) {
      rustyirc_send(c, "CAP END");
      s->negotiating = false;
   }

   if (*result) {
      rustyirc_event_emit("irc.sasl", c, result);
   }
}
static void rustyirc_capabilities_poll_impl(irc_conn_t *c) {
   irc_cap_state_t *s = state(c, false);

   if (s && s->sasl_deadline && mono_ms() >= s->sasl_deadline) {
      if (s->sasl_wait) {
         rustyirc_send(c, "AUTHENTICATE *");
      }
      sasl_finish(c, s, "SASL authentication timed out");
   }
}
static bool sasl_response(irc_conn_t *c) {
   __typeof__(c->credentials) * server = &c->credentials;

   if (!strcasecmp(server->sasl_mechanism, "EXTERNAL")) {
      return rustyirc_send(c, "AUTHENTICATE +");
   }
   unsigned char plain[LOGINLEN + PASSLEN + 3];
   size_t user = strlen(server->sasl_user), pass = strlen(server->sasl_pass);
   plain[0] = 0;
   memcpy(plain + 1, server->sasl_user, user);
   plain[user + 1] = 0;
   memcpy(plain + user + 2, server->sasl_pass, pass);
   static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   char encoded[4 * ((sizeof(plain) + 2) / 3) + 1];
   size_t bytes = user + pass + 2, length = 0;

   for (size_t i = 0 ; i < bytes ; i += 3) {
      uint32_t value = (uint32_t) plain[i] << 16;

      if (i + 1 < bytes) {
         value |= (uint32_t) plain[i + 1] << 8;
      }

      if (i + 2 < bytes) {
         value |= plain[i + 2];
      }
      encoded[length++] = alphabet[value >> 18];
      encoded[length++] = alphabet[(value >> 12) & 63];
      encoded[length++] = i + 1 < bytes ? alphabet[(value >> 6) & 63] : '=';
      encoded[length++] = i + 2 < bytes ? alphabet[value & 63] : '=';
   }

   encoded[length] = 0;
   memset(plain, 0, sizeof(plain));
   bool sent = true;

   for (size_t i = 0 ; sent && i < length ; i += 400) {
      sent = rustyirc_send(c, "AUTHENTICATE %.*s", (int)(length - i > 400 ? 400 : length - i), encoded + i);
   }

   if (sent && length % 400 == 0) {
      sent = rustyirc_send(c, "AUTHENTICATE +");
   }
   memset(encoded, 0, length);

   return sent;
}

static void rustyirc_capabilities_message_impl(irc_conn_t *c, const irc_message_t *m) {
   if (!c || !m || m->argc < 2) {
      return;
   }
   irc_cap_state_t *s = state(c, true);

   if (!s) {
      return;
   }
   bool old = s->rusty;

   if (s->sasl_wait && !strcasecmp(m->argv[0], "AUTHENTICATE")) {
      if (s->sasl_sent || strcmp(m->argv[1], "+") || !sasl_response(c)) {
         rustyirc_send(c, "AUTHENTICATE *");
         sasl_finish(c, s, "SASL authentication aborted");
      } else {
         s->sasl_sent = true;
      }

      return;
   }

   if (s->sasl_wait && (!strcmp(m->argv[0], "903") || !strcmp(m->argv[0], "907"))) {
      sasl_finish(c, s, "SASL authentication successful");

      return;
   }

   if (s->sasl_wait && (!strcmp(m->argv[0], "902") || !strcmp(m->argv[0], "904") || !strcmp(m->argv[0], "905") || !strcmp(m->argv[0], "906"))) {
      sasl_finish(c, s, "SASL authentication failed");

      return;
   }

   if (!strcmp(m->argv[0], "005")) {
      /* The last parameter is the human-readable ISUPPORT explanation. */
      for (int i = 2 ; i < m->argc - 1 ; i++) {
         const char *token = m->argv[i];
         char key[IRC_MSGLEN + 1];
         const char *name = token + (*token == '-');
         size_t length = strcspn(name, "=");

         if (!length || length >= sizeof(key)) {
            continue;
         }

         for (size_t j = 0 ; j < length ; j++) {
            key[j] = toupper((unsigned char)name[j]);
         }

         key[length] = '\0';

         if (*token == '-') {
            dict_del(s->properties, key);
         } else {
            dict_add(s->properties, key, name[length] == '=' ? name + length + 1 : "");
         }

         if (!strcasecmp(token, "RUSTYRIG") || !strncasecmp(token, "RUSTYRIG=", 9)) {
            s->rusty = true;
         } else if (!strcasecmp(token, "-RUSTYRIG")) {
            s->rusty = false;
         } else if (!strncmp(token, "PREFIX=(", 8)) {
            const char *end = strchr(token + 8, ')');
            size_t n = end ? (size_t)(end - token - 8) : 0;

            if (n && n < sizeof(s->modes) && strlen(end + 1) == n) {
               memcpy(s->modes, token + 8, n);
               s->modes[n] = '\0';
               strcpy(s->symbols, end + 1);
               memset(s->display, 0, sizeof(s->display));

               for (size_t j = 0 ; j < n ; j++) {
                  s->display[j][0] = s->symbols[j];
               }
            }
         } else if (!strncmp(token, "CHANMODES=", 10) && strlen(token + 10) < sizeof(s->chanmodes)) {
            strcpy(s->chanmodes, token + 10);
         }
      }
   } else if (!strcasecmp(m->argv[0], "CAP") && m->argc >= 4) {
      const char *command = m->argv[2];
      bool listing = !strcasecmp(command, "LS");
      bool listing_enabled = !strcasecmp(command, "LIST");
      bool more = m->argc >= 5 && !strcmp(m->argv[3], "*");

      if (listing && !s->listing) {
         dict *fresh = dict_new();

         if (!fresh) {
            return;
         }
         dict_free(s->capabilities);
         s->capabilities = fresh;
         s->requests[0] = '\0';
      }

      if (listing_enabled && !s->listing_enabled) {
         dict *fresh = dict_new();

         if (!fresh) {
            return;
         }
         dict_free(s->enabled);
         s->enabled = fresh;
      }

      if (listing) {
         s->listing = more;
      }

      if (listing_enabled) {
         s->listing_enabled = more;
      }
      char *copy = strdup(m->argv[m->argc - 1]), *save = NULL;

      for (char *token = copy ? strtok_r(copy, " ", &save) : NULL ; token ; token = strtok_r(NULL, " ", &save)) {
         bool remove = *token == '-';
         const char *name = token + remove;
         size_t length = strcspn(name, "=");
         bool rusty = length == 8 && !strncasecmp(name, "RUSTYRIG", length);
         bool multi = length == 12 && !strncasecmp(name, "multi-prefix", length);
         char key[IRC_MSGLEN + 1];

         if (!length || length >= sizeof(key)) {
            continue;
         }
         memcpy(key, name, length);
         key[length] = '\0';
         const char *value = name[length] == '=' ? name + length + 1 : "";

         if (listing || !strcasecmp(command, "NEW")) {
            if (remove) {
               dict_del(s->capabilities, key);
            } else {
               dict_add(s->capabilities, key, value);
            }
         } else if (!strcasecmp(command, "DEL")) {
            dict_del(s->capabilities, key);
            dict_del(s->enabled, key);
         } else if (!strcasecmp(command, "ACK") || listing_enabled) {
            if (remove) {
               dict_del(s->enabled, key);
            } else {
               dict_add(s->enabled, key, value);
            }
         } else if (!strcasecmp(command, "NAK")) {
            dict_del(s->enabled, key);
         }

         if (!strcmp(key, "sasl") && listing && sasl_configured(c)) {
            s->sasl_requested = true;
            s->sasl_deadline = mono_ms() + 30000;
         }

         if (!strcasecmp(command, "LS") && !remove && (rusty || multi || (!strcmp(key, "sasl") && sasl_configured(c)))) {
            char cap[32];
            snprintf(cap, sizeof(cap), "%.*s", (int)length, name);

            if (!strstr(s->requests, cap)) {
               if (*s->requests) {
                  strlcat(s->requests, " ", sizeof(s->requests));
               }
               strlcat(s->requests, cap, sizeof(s->requests));
            }
         } else if (rusty && !strcasecmp(command, "ACK")) {
            s->rusty = !remove;
         } else if (rusty && !strcasecmp(command, "DEL")) {
            s->rusty = false;
         }
      }

      free(copy);

      if (s->negotiating && !strcasecmp(command, "LS") && !(m->argc >= 5 && !strcmp(m->argv[3], "*"))) {
         if (sasl_configured(c) && !rustyirc_server_capability(c, "sasl")) {
            rustyirc_event_emit("irc.sasl", c, "Server does not advertise SASL authentication");
         }

         if (*s->requests) {
            rustyirc_send(c, "CAP REQ :%s", s->requests);
         } else {
            rustyirc_send(c, "CAP END");
            s->negotiating = false;
         }
      } else if (s->negotiating && (!strcasecmp(command, "ACK") || !strcasecmp(command, "NAK"))) {
         if (!strcasecmp(command, "ACK") && s->sasl_requested && !rustyirc_capability_enabled(c, "sasl")) {
            return;             // Another requested capability may be acknowledged first.
         }

         if (s->sasl_wait) {
            return;
         }

         if (!strcasecmp(command, "ACK") && sasl_configured(c) && rustyirc_capability_enabled(c, "sasl")) {
            const char *mechanism = !strcasecmp(c->credentials.sasl_mechanism, "EXTERNAL") ? "EXTERNAL" : "PLAIN";

            if (rustyirc_send(c, "AUTHENTICATE %s", mechanism)) {
               s->sasl_wait = true;
               s->sasl_deadline = mono_ms() + 30000;
            } else {
               sasl_finish(c, s, "Unable to start SASL authentication");
            }
         } else {
            sasl_finish(c, s, sasl_configured(c) ? "SASL capability rejected" : "");
         }
      }
   } else if (!strcasecmp(m->argv[0], "CAPAB") && m->argc >= 2) {
      for (int i = 1 ; i < m->argc ; i++) {
         char *copy = strdup(m->argv[i]), *save = NULL;

         for (char *token = copy ? strtok_r(copy, " ", &save) : NULL ; token ; token = strtok_r(NULL, " ", &save)) {
            bool remove = *token == '-';
            char *name = token + remove;
            char *value = strchr(name, '=');

            if (value) {
               *value++ = '\0';
            }

            if (*name) {
               if (remove) {
                  dict_del(s->capabs, name);
               } else {
                  dict_add(s->capabs, name, value ? value : "");
               }
            }
         }

         free(copy);
      }
   } else if (!strcmp(m->argv[0], "421") && m->argc >= 3 && !strcasecmp(m->argv[2], "CAP")) {
      s->negotiating = false;
   }

   if (old != s->rusty) {
      rustyirc_event_emit("irc.capabilities", c, "");
   }
}

void rustyirc_capabilities_poll(irc_conn_t *c) {
   if (!rustyirc_connection_hold(c)) {
      return;
   }
   rustyirc_capabilities_poll_impl(c);
   rustyirc_connection_release(c);
}

void rustyirc_capabilities_message(irc_conn_t *c, const irc_message_t *message) {
   if (!rustyirc_connection_hold(c)) {
      return;
   }
   rustyirc_capabilities_message_impl(c, message);
   rustyirc_connection_release(c);
}
