//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Stuff strictly related to irc client mode
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
#include <librrprotocol/rrprotocol.h>

rrconn_t *irc_cli_connect(server_cfg_t *srv) {
   // TLS requires a transport backend; never silently send credentials in plaintext.
   if (!srv || !srv->host[0] || srv->port < 1 || srv->port > 65535 || srv->tls) {
      return NULL;
   }
   rrconn_t *cptr = calloc(1, sizeof(*cptr));

   if (!cptr) {
      return NULL;
   }
   cptr->fd = -1;
   cptr->server = srv;
   snprintf(cptr->nick, sizeof(cptr->nick), "%s", srv->nick[0] ? srv->nick : "nonick");
   cptr->sent_login = false;

   struct addrinfo hints, *res, *rp;
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;

   char portbuf[16];
   snprintf(portbuf, sizeof(portbuf), "%d", srv->port);

   if (getaddrinfo(srv->host, portbuf, &hints, &res) != 0) {
      free(cptr);

      return NULL;
   }
   int fd = -1;

   for (rp = res ; rp != NULL ; rp = rp->ai_next) {
      fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

      if (fd < 0) {
         continue;
      }
      int flags = fcntl(fd, F_GETFL);

      if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
         Log(LOG_WARN, "irc.net", "Unable to make socket nonblocking: %s", strerror(errno));
         close(fd);
         fd = -1;
         continue;
      }

      int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
      Log(LOG_DEBUG, "irc.net", "connect(fd=%d) rc=%d errno=%d", fd, rc, errno);

      if (rc == 0) {
         cptr->connected = now;
         break;
      } else if (errno == EINPROGRESS) {
         cptr->connected = false;
         break;
      }
      close(fd);
      fd = -1;
   }

   freeaddrinfo(res);

   if (fd < 0) {
      free(cptr);

      return NULL;
   }
   cptr->fd = fd;

   return cptr;
}

#if defined(USE_MONGOOSE)
#include <rrclient/irc.h>
extern struct mg_str tls_ca_path_str;

/* The application supplies and owns cptr and its server configuration. */
void irc_mongoose_handler(struct mg_connection *c, int ev, void *data) {
   rrconn_t *cptr = c ? c->fn_data : NULL;

   if (!cptr) {
      return;
   }

   if (ev == MG_EV_POLL) {
      irc_capabilities_poll(cptr);
   }

   if (ev == MG_EV_OPEN) {
      cptr->conn = c;
      cptr->fd = -1;
   } else if (ev == MG_EV_CONNECT) {
      cptr->conn = c;

      if (c->is_tls) {
         struct mg_tls_opts opts = {
            .name = mg_str(cptr->server->host),
            .ca = tls_ca_path_str
         };
         mg_tls_init(c, &opts);
      } else {
         cptr->connected = now;
         irc_client_register(cptr);
      }
   } else if (ev == MG_EV_TLS_HS) {
      cptr->connected = now;
      irc_client_register(cptr);
   } else if (ev == MG_EV_READ) {
      for (size_t offset = 0 ; offset < c->recv.len && !c->is_closing ; ) {
         size_t length = c->recv.len - offset;

         if (length > IRC_MSGLEN - 1) {
            length = IRC_MSGLEN - 1;
         }
         irc_receive(cptr, c->recv.buf + offset, length);
         offset += length;
      }

      mg_iobuf_del(&c->recv, 0, c->recv.len);
   } else if (ev == MG_EV_ERROR) {
      dict *d = dict_new();
      dict_add(d, "error.msg", data ? data : "IRC transport error");
      event_emit_dict("irc.error", cptr, d);
      dict_free(d);
      c->is_closing = 1;
   } else if (ev == MG_EV_CLOSE && cptr->conn == c) {
      irc_capabilities_clear(cptr);
      cptr->conn = NULL;
      cptr->connected = 0;
      cptr->authenticated = false;
      cptr->sent_login = false;
      event_emit("irc.disconnected", cptr, "");
   }
}
#endif

#include <rrclient/irc.h>
static bool irc_target_valid(const char *target) {
   if (!target || !*target || *target == ':') {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)target ; *p ; p++) {
      if (*p <= ' ' || *p == 127) {
         return false;
      }
   }

   return true;
}

/* Split only chat text, never arbitrary commands or CTCP requests. */
static bool irc_send_chat(rrconn_t *cptr, dict *request, const char *target, const char *text) {
   const char *kind = dict_get(request, "talk.msg_type", "pub");
   bool action = !strcmp(kind, "action");
   const char *command = !strcmp(kind, "notice") ? "NOTICE" : "PRIVMSG";
   size_t overhead = strlen(command) + strlen(target) + 5 + (action ? 9 : 0);
   size_t limit = irc_line_limit(cptr);

   if (overhead >= limit || strchr(text, '\r') || strchr(text, '\n') || (!action && strchr(text, '\001'))) {
      /* Ordinary CTCP (DCC etc.) must retain its single-message framing. */
      if (!action && strchr(text, '\001') && !strchr(text, '\r') && !strchr(text, '\n')) {
         bool sent = irc_send(cptr, "%s %s :%s", command, target, text);

         if (sent) {
            event_emit_dict("irc.sent", cptr, request);
         }

         return sent;
      }

      return false;
   }
   size_t remaining = strlen(text), budget = limit - overhead;
   dict *echo = dict_new();

   if (!echo || dict_merge(echo, request)) {
      dict_free(echo);

      return false;
   }
   do {
      size_t length = remaining < budget ? remaining : budget;

      if (length < remaining) {
         /* A continuation byte belongs to the preceding UTF-8 character. */
         while (length && ((unsigned char)text[length] & 0xc0) == 0x80) {
            length--;
         }

         if (!length) {
            dict_free(echo);

            return false;
         }

         /* Prefer word boundaries, retaining every original byte. */
         for (size_t i = length ; i > 0 ; i--) {
            if (text[i - 1] == ' ') {
               length = i;
               break;
            }
         }
      }
      char chunk[IRC_MSGLEN + 1];
      memcpy(chunk, text, length);
      chunk[length] = '\0';
      bool sent = action ? irc_send(cptr, "%s %s :\001ACTION %s\001", command, target, chunk) : irc_send(cptr, "%s %s :%s", command, target, chunk);

      if (!sent) {
         dict_free(echo);

         return false;
      }
      dict_add(echo, "talk.data", chunk);
      event_emit_dict("irc.sent", cptr, echo);
      text += length;
      remaining -= length;
   } while (remaining);
   dict_free(echo);

   return true;
}

/* Serialize common client chat requests using IRC wire semantics. */
bool irc_send_dict(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      return false;
   }
   const char *type = dict_get(d, "msg.type", "");
   const char *cmd = dict_get(d, "talk.cmd", "");
   const char *target = dict_get(d, "talk.target", "");
   const char *text = dict_get(d, "talk.data", "");

   if (!strcmp(type, "ping")) {
      return irc_send(cptr, "PING :%ld", (long)now);
   }

   if (!strcmp(type, "talk")) {
      if (!strcmp(cmd, "list")) {
         return irc_send(cptr, "LIST");
      }

      if (!strcmp(cmd, "join") && irc_target_valid(target)) {
         return irc_send(cptr, "JOIN %s", target);
      }

      if (!strcmp(cmd, "part") && irc_target_valid(target)) {
         return irc_send(cptr, "PART %s", target);
      }

      if (!strcmp(cmd, "topic") && irc_target_valid(target)) {
         return *text ? irc_send(cptr, "TOPIC %s :%s", target, text) : irc_send(cptr, "TOPIC %s", target);
      }

      if (!strcmp(cmd, "whois")) {
         const char *who = dict_get(d, "talk.user", target);

         return irc_target_valid(who) && irc_send(cptr, "WHOIS %s", who);
      }

      if (!strcmp(cmd, "msg") && irc_target_valid(target)) {
         return irc_send_chat(cptr, d, target, text);
      }
   }
   event_emit_dict("irc.command.unsupported", cptr, d);

   return false;
}
