#include "internal.h"
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef ESP_PLATFORM
#include <lwip/inet.h>
#else
#include <arpa/inet.h>
#endif

/* DCC AUDIO v1/v2 carry a 30-byte master key as lowercase hexadecimal. */
#define IRC_AUDIO_KEY_BYTES 30

static bool atom(const char *text, size_t maximum) {
   if (!text || !*text || strlen(text) > maximum) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)text ; *p ; p++) {
      if (*p <= ' ' || *p >= 127 || *p == ':') {
         return false;
      }
   }

   return true;
}
static bool number(const char *text, unsigned maximum, unsigned *value) {
   if (!text || !*text) {
      return false;
   }
   unsigned result = 0;

   for (const char *p = text ; *p ; p++) {
      if (!isdigit((unsigned char)*p) || result > (maximum - (*p - '0')) / 10) {
         return false;
      }
      result = result * 10 + (*p - '0');

      if (result > maximum) {
         return false;
      }
   }

   *value = result;

   return true;
}
dict *rustyirc_dcc_audio_decode(const char *payload) {
   if (!payload) {
      return NULL;
   }
   size_t length = strlen(payload);

   if (length < 2 || length >= IRC_MSGLEN || payload[0] != '\001' || payload[length - 1] != '\001') {
      return NULL;
   }
   char copy[IRC_MSGLEN];
   memcpy(copy, payload + 1, length - 2);
   copy[length - 2] = 0;
   char *fields[15], *cursor = copy;
   unsigned count = 0;
   while (count < 15) {
      unsigned i = count++;
      fields[i] = cursor;
      char *space = strchr(cursor, ' ');

      if (space) {
         *space = 0;
         cursor = space + 1;
      }

      if (!atom(fields[i], 127)) {
         return NULL;
      }

      if (!space) {
         break;
      }

      if (count == 15) {
         return NULL;
      }
   }
   bool voice = count == 15 && !strcmp(fields[2], "2") && !strcmp(fields[13], "voice") && !strcmp(fields[14], "s16l");

   if (count < 13 || strcasecmp(fields[0], "DCC") || strcasecmp(fields[1], "AUDIO") || (!voice && (count != 13 || strcmp(fields[2], "1")))) {
      return NULL;
   }
   struct in_addr address;

   if (inet_pton(AF_INET, fields[3], &address) != 1) {
      return NULL;
   }
   unsigned port, ssrc, stream, owner, direction;

   if (!number(fields[4], 65535, &port) || !port || !number(fields[8], 0x7fffffffU, &ssrc) || !ssrc ||
      !number(fields[9], 255, &stream) || !stream || !number(fields[10], 255, &owner) || !owner ||
      !number(fields[11], 1, &direction) || strlen(fields[5]) != IRC_AUDIO_KEY_BYTES * 2 ||
      strlen(fields[6]) >= 64 || strlen(fields[7]) >= 64 || (strcmp(fields[12], "null-sha1-80") && strcmp(fields[12], "aes128cm-sha1-80"))) {
      return NULL;
   }

   for (const char *p = fields[5] ; *p ; p++) {
      if (!isdigit((unsigned char)*p) && (*p < 'a' || *p > 'f')) {
         return NULL;
      }
   }

   dict *d = dict_new();

   if (!d) {
      return NULL;
   }
   char endpoint[96];
   snprintf(endpoint, sizeof(endpoint), "rtp://%s:%u", fields[3], port);
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "subscribed");
   dict_add(d, "media.transport", "direct");
   dict_add(d, "media.direct", endpoint);
   dict_add(d, "media.direct-key", fields[5]);
   dict_add(d, "media.parent-session", fields[6]);
   dict_add(d, "media.chan-uuid", fields[7]);
   dict_add_ulong(d, "media.direct-ssrc", ssrc);
   dict_add_ulong(d, "media.stream", stream);
   dict_add_ulong(d, "media.owner-stream", owner);
   dict_add_ulong(d, "media.dir", direction);
   dict_add(d, "media.direct-security", fields[12]);

   if (voice) {
      dict_add(d, "media.kind", "voice");
      dict_add(d, "media.codec", "s16l");
   }

   return d;
}
char *rustyirc_dcc_audio_encode(const char *parent, dict *grant) {
   if (!grant || !atom(parent, 63)) {
      return NULL;
   }

   if (!strcmp(dict_get(grant, "media.kind", ""), "voice") && strcmp(dict_get(grant, "media.codec", ""), "s16l")) {
      return NULL;
   }
   char ip[64], extra;
   unsigned port;

   if (sscanf(dict_get(grant, "media.direct", ""), "rtp://%63[^:]:%u%c", ip, &port, &extra) != 2) {
      return NULL;
   }
   char payload[IRC_MSGLEN];
   int length = snprintf(payload, sizeof(payload), "\001DCC AUDIO %s %s %u %s %s %s %lu %lu %lu %lu %s%s\001", !strcmp(dict_get(grant, "media.kind", ""),
      "voice") ? "2" : "1", ip, port, dict_get(grant, "media.direct-key", ""), parent, dict_get(grant, "media.chan-uuid", ""), dict_get_ulong(grant,
      "media.direct-ssrc", 0), dict_get_ulong(grant, "media.stream", 0), dict_get_ulong(grant, "media.owner-stream", dict_get_ulong(grant, "media.stream", 0)),
      dict_get_ulong(grant, "media.dir", 2), dict_get(grant, "media.direct-security", ""), !strcmp(dict_get(grant, "media.kind", ""), "voice") ? " voice s16l" :
      "");

   if (length < 0 || length >= (int)sizeof(payload)) {
      return NULL;
   }
   dict *checked = rustyirc_dcc_audio_decode(payload);

   if (!checked) {
      return NULL;
   }
   dict_free(checked);

   return strdup(payload);
}
bool rustyirc_send_dcc_audio(irc_conn_t *client, const char *target, const char *parent, dict *grant) {
   if (!client || !atom(target, NICKLEN) || strpbrk(target, ",#!&")) {
      return false;
   }
   char *payload = rustyirc_dcc_audio_encode(parent, grant);

   if (!payload) {
      return false;
   }
   bool sent = rustyirc_send(client, "PRIVMSG %s :%s", target, payload);
   free(payload);

   return sent;
}
bool rustyirc_dcc_audio_message(irc_conn_t *client, const irc_message_t *message) {
   if (!client || !message || message->argc < 3 || !message->argv[0] || !message->argv[2] ||
      (strcasecmp(message->argv[0], "PRIVMSG") && strcasecmp(message->argv[0], "NOTICE")) || strncasecmp(message->argv[2], "\001DCC AUDIO", 10)) {
      return false;
   }

   /* Consume invalid/public referrals too: private keys must never enter chat logs. */
   if (!message->prefix || !*message->prefix || !message->argv[1] || strcasecmp(message->argv[1], client->nick)) {
      return true;
   }
   dict *d = rustyirc_dcc_audio_decode(message->argv[2]);

   if (!d) {
      return true;
   }
   dict_add(d, "irc.prefix", message->prefix);
   dict_add(d, "irc.target", message->argv[1]);
   rustyirc_event_emit_dict("irc.dcc.audio", client, d);
   dict_free(d);

   return true;
}
