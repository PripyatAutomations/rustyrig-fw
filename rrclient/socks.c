// Native TCP proxy negotiation. No protocol or frontend state lives here.
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <rrclient/socks.h>

#ifdef USE_MONGOOSE

enum {
   SOCKS_OFF, SOCKS_GREETING, SOCKS_AUTH, SOCKS_CONNECT, SOCKS_READY, SOCKS_FAILED
};

bool rrclient_socks_init(rrclient_socks_t *s, const char *url, const char *user, const char *pass, const rr_server_url_t *destination) {
   memset(s, 0, sizeof(*s));

   if (!url || !*url) {
      return true;
   }
   const char *address;

   if (!strncmp(url, "socks5h://", 10)) {
      address = url + 10;
   } else if (!strncmp(url, "socks5://", 9)) {
      address = url + 9;
   } else {
      return false;
   }
   char parsed_url[1024];
   int n = snprintf(parsed_url, sizeof(parsed_url), "irc://%s", address);
   rr_server_url_t proxy;

   if (n < 0 || (size_t)n >= sizeof(parsed_url) || !rr_server_url_parse(parsed_url, &proxy)) {
      return false;
   }
   const char *suffix = *address == '[' ? strchr(address, ']') + 1 : address + strcspn(address, ":/");

   if (*suffix != ':') {
      proxy.port = 1080;
   }
   user = user ? user : "";
   pass = pass ? pass : "";

   if (strlen(user) > 255 || strlen(pass) > 255 || (*user && !*pass) || (!*user && *pass) ||
      !destination || !*destination->host || strlen(destination->host) > 255) {
      return false;
   }
   snprintf(s->url, sizeof(s->url), "tcp://%s%s%s:%u", proxy.ipv6 ? "[" : "", proxy.host, proxy.ipv6 ? "]" : "", proxy.port);
   memcpy(s->host, destination->host, strlen(destination->host) + 1);
   snprintf(s->user, sizeof(s->user), "%s", user);
   snprintf(s->pass, sizeof(s->pass), "%s", pass);
   s->port = destination->port;
   s->stage = SOCKS_GREETING;
   s->deadline = now + 30;

   return true;
}

static int fail(rrclient_socks_t *s, struct mg_connection *c, const char *error) {
   s->stage = SOCKS_FAILED;
   mg_error(c, "SOCKS5: %s", error);

   return -1;
}

static void connect_request(rrclient_socks_t *s, struct mg_connection *c) {
   unsigned char packet[262] = {
      5, 1, 0, 3
   };
   struct in_addr ipv4;
   struct in6_addr ipv6;
   size_t n;

   if (inet_pton(AF_INET, s->host, &ipv4) == 1) {
      packet[3] = 1;
      memcpy(packet + 4, &ipv4, 4);
      n = 8;
   } else if (inet_pton(AF_INET6, s->host, &ipv6) == 1) {
      packet[3] = 4;
      memcpy(packet + 4, &ipv6, 16);
      n = 20;
   } else {
      packet[4] = (unsigned char)strlen(s->host);
      memcpy(packet + 5, s->host, packet[4]);
      n = 5 + packet[4];
   }
   packet[n++] = (unsigned char)(s->port >> 8);
   packet[n++] = (unsigned char)s->port;
   s->stage = SOCKS_CONNECT;
   mg_send(c, packet, n);
}

int rrclient_socks_event(rrclient_socks_t *s, struct mg_connection *c, int event) {
   if (s->stage == SOCKS_FAILED) {
      return -1;
   }

   if (event == MG_EV_POLL && now >= s->deadline) {
      return fail(s, c, "handshake timed out");
   }

   if (event == MG_EV_CONNECT) {
      unsigned char greeting[] = {
         5, 1, *s->user ? 2 : 0
      };
      mg_send(c, greeting, sizeof(greeting));
   }

   if (event != MG_EV_READ) {
      return 0;
   }
   while (!c->is_closing) {
      unsigned char *p = c->recv.buf;
      size_t n = c->recv.len;

      if (s->stage == SOCKS_GREETING) {
         if (n < 2) {
            return 0;
         }

         if (p[0] != 5 || p[1] != (*s->user ? 2 : 0)) {
            return fail(s, c, "authentication method rejected");
         }
         mg_iobuf_del(&c->recv, 0, 2);

         if (*s->user) {
            unsigned char auth[513];
            size_t u = strlen(s->user), v = strlen(s->pass);
            auth[0] = 1;
            auth[1] = (unsigned char)u;
            memcpy(auth + 2, s->user, u);
            auth[2 + u] = (unsigned char)v;
            memcpy(auth + 3 + u, s->pass, v);
            s->stage = SOCKS_AUTH;
            mg_send(c, auth, 3 + u + v);
         } else {
            connect_request(s, c);
         }
      } else if (s->stage == SOCKS_AUTH) {
         if (n < 2) {
            return 0;
         }

         if (p[0] != 1 || p[1] != 0) {
            return fail(s, c, "authentication failed");
         }
         mg_iobuf_del(&c->recv, 0, 2);
         connect_request(s, c);
      } else if (s->stage == SOCKS_CONNECT) {
         if (n < 4) {
            return 0;
         }

         if (p[0] != 5 || p[2] != 0) {
            return fail(s, c, "invalid CONNECT reply");
         }

         if (p[1] != 0) {
            static const char *errors[] = {
               "success", "proxy failure", "connection forbidden", "network unreachable",
               "host unreachable", "connection refused", "TTL expired", "command unsupported", "address unsupported"
            };

            return fail(s, c, p[1] < sizeof(errors) / sizeof(errors[0]) ? errors[p[1]] : "CONNECT rejected");
         }
         size_t length;

         if (p[3] == 1) {
            length = 10;
         } else if (p[3] == 4) {
            length = 22;
         } else if (p[3] == 3) {
            if (n < 5) {
               return 0;
            }

            if (!p[4]) {
               return fail(s, c, "empty reply hostname");
            }
            length = 7 + p[4];
         } else {
            return fail(s, c, "invalid reply address type");
         }

         if (n < length) {
            return 0;
         }
         mg_iobuf_del(&c->recv, 0, length);
         s->stage = SOCKS_READY;

         return 1;
      } else {
         return 0;
      }
   }
   return -1;
}
#endif
