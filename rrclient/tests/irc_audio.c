#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <rrclient/irc.h>
static unsigned offers;
static void offer(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)client;
   (void)user;
   dict *d = json2dict(data);
   assert(d);
   assert(!strcmp(dict_get(d, "media.parent-session", ""), "parent-session"));
   assert(!strcmp(dict_get(d, "media.direct", ""), "rtp://127.0.0.1:8422"));
   assert(!strcmp(dict_get(d, "irc.prefix", ""), "radio!u@host"));
   offers++;
   dict_free(d);
}
int main(void) {
   dict *grant = dict_new();
   assert(grant);
   dict_add(grant, "media.direct", "rtp://127.0.0.1:8422");
   dict_add(grant, "media.direct-key", "000000000000000000000000000000000000000000000000000000000000");
   dict_add(grant, "media.chan-uuid", "channel-uuid");
   dict_add_ulong(grant, "media.direct-ssrc", 42);
   dict_add_ulong(grant, "media.stream", 128);
   dict_add_ulong(grant, "media.owner-stream", 1);
   dict_add_ulong(grant, "media.dir", 0);
   dict_add(grant, "media.direct-security", "null-sha1-80");
   char *payload = irc_dcc_audio_encode("parent-session", grant);
   assert(payload);
   dict *copy = irc_dcc_audio_decode(payload);
   assert(copy);
   assert(dict_get_ulong(copy, "media.stream", 0) == 128 && dict_get_ulong(copy, "media.owner-stream", 0) == 1);
   dict_free(copy);
   int sockets[2];
   assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
   rrconn_t client = {
      .fd = sockets[0], .connected = 1
   };
   strcpy(client.nick, "me");
   assert(irc_send_dcc_audio(&client, "me", "parent-session", grant));
   struct timeval timeout = {
      .tv_sec = 2
   };
   setsockopt(sockets[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
   char wire[1024];
   ssize_t length = recv(sockets[1], wire, sizeof(wire) - 1, 0);
   assert(length > 0);
   wire[length] = 0;
   assert(strstr(wire, payload));
   assert(!irc_send_dcc_audio(&client, "#public", "parent-session", grant));
   assert(!irc_send_dcc_audio(&client, "me\r\nQUIT", "parent-session", grant));
   assert(!irc_dcc_audio_encode("bad parent", grant));
   event_init();
   event_on("irc.dcc.audio", offer, NULL);
   assert(!irc_init());
   snprintf(wire, sizeof(wire), ":radio!u@host PRIVMSG me :%s", payload);
   assert(!irc_process_message(&client, wire));
   assert(offers == 1);
   snprintf(wire, sizeof(wire), ":radio!u@host NOTICE me :%s", payload);
   assert(!irc_process_message(&client, wire));
   assert(offers == 2);
   snprintf(wire, sizeof(wire), ":radio!u@host PRIVMSG #public :%s", payload);
   assert(!irc_process_message(&client, wire));
   assert(offers == 2);
   dict_add(grant, "media.kind", "voice");
   dict_add(grant, "media.codec", "s16l");
   char *voice = irc_dcc_audio_encode("parent-session", grant);
   assert(voice);
   assert(strstr(voice, "DCC AUDIO 2 ") && strstr(voice, " voice s16l"));
   copy = irc_dcc_audio_decode(voice);
   assert(copy);
   assert(!strcmp(dict_get(copy, "media.kind", ""), "voice"));
   assert(!strcmp(dict_get(copy, "media.codec", ""), "s16l"));
   dict_free(copy);
   snprintf(wire, sizeof(wire), ":radio!u@host PRIVMSG me :%s", voice);
   assert(!irc_process_message(&client, wire) && offers == 3);
   char *codec = strstr(voice, "s16l");
   assert(codec);
   memcpy(codec, "opus", 4);
   assert(!irc_dcc_audio_decode(voice));
   free(voice);
   const char *bad[] = {
      "\001DCC AUDIO 2 127.0.0.1 8422 key parent uuid 42 128 1 0 null-sha1-80\001",
      "\001DCC AUDIO 1 host 8422 key parent uuid 42 128 1 0 null-sha1-80\001",
      "\001DCC AUDIO 1 127.0.0.1 0 key parent uuid 42 128 1 0 null-sha1-80\001",
      "\001DCC AUDIO 1 127.0.0.1 8422 key parent uuid 42 128 1 0 null-sha1-80\001 extra",
      "DCC AUDIO 1 127.0.0.1 8422 key parent uuid 42 128 1 0 null-sha1-80",
   };

   for (unsigned i = 0 ; i < sizeof(bad) / sizeof(bad[0]) ; i++) {
      assert(!irc_dcc_audio_decode(bad[i]));
   }

   const char *invalid_keys[] = {
      "media.direct-key", "media.direct-ssrc", "media.stream", "media.owner-stream", "media.dir", "media.direct-security"
   };
   const char *invalid_values[] = {
      "zz", "2147483648", "256", "0", "2", "unknown"
   };

   for (unsigned i = 0 ; i < 6 ; i++) {
      const char *old = dict_get(grant, invalid_keys[i], "");
      char *saved = strdup(old);
      dict_add(grant, invalid_keys[i], invalid_values[i]);
      assert(!irc_dcc_audio_encode("parent-session", grant));
      dict_add(grant, invalid_keys[i], saved);
      free(saved);
   }

   free(payload);
   dict_free(grant);
   irc_shutdown();
   event_shutdown();
   close(sockets[0]);
   close(sockets[1]);
   puts("PASS: DCC AUDIO roundtrip, private IRC delivery, session/channel metadata, malformed/injected referral rejection");
}
