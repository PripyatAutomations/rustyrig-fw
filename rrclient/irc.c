//
// irc.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/traffic.h>
#include <rrclient/irc.h>
#include <rrclient/connman.h>
#include <poll.h>
#include <rrclient/irc.dcc.h>
#include <librrprotocol/server.events.h>

static void irc_close_connection(rrconn_t *cptr) {
#if defined(USE_MONGOOSE)

   if (cptr->conn) {
      cptr->conn->is_closing = 1;

      return;
   }
#endif

   if (cptr->fd < 0) {
      return;
   }
   close(cptr->fd);
   cptr->fd = -1;
   cptr->connected = 0;
   cptr->authenticated = false;
   cptr->sent_login = false;
   cptr->sendq[0] = cptr->recvq[0] = '\0';
   irc_capabilities_clear(cptr);
   event_emit("irc.disconnected", cptr, "");
}

static void send_request(const char *event, const void *payload, size_t size, rrconn_t *c, void *user) {
   (void)event;
   (void)user;

   if (!c || c->is_ws || !c->server || size != sizeof(rr_server_io_t)) {
      return;
   }
   rr_server_io_t *request = (rr_server_io_t *) payload;

   if (!request->handled) {
      request->handled = true;
      request->result = irc_send_dict(c, request->message);
   }
}
void rrclient_irc_adapter_shutdown(void) {
   event_off_binary(RR_SERVER_SEND_EVENT, send_request, NULL);
}
bool irc_init(void) {
   irc_dcc_init();
   event_off_binary(RR_SERVER_SEND_EVENT, send_request, NULL);
   event_on_binary(RR_SERVER_SEND_EVENT, send_request, NULL);

   if (!irc_register_default_callbacks() || !irc_register_default_numeric_callbacks()) {
      irc_shutdown();

      return true;
   }

   return false;
}

//
// This will create a dict containing a restricted set of state things which
// we'll allow
// substituting in log files, messages, etc.
dict *irc_generate_vars(rrconn_t *cptr, const char *chan) {
   dict *d = dict_new();

   if (!d) {
      Log(LOG_CRIT, "irc", "OOM in irc_generate_vars");

      return NULL;
   }

   if (cptr) {
      dict_add(d, "nick", cptr->nick);
   }

   if (chan) {
      dict_add(d, "chan", (char *)chan);
   }

   return d;
}

// Send as much of this user's sendq as we can
static void irc_try_send(rrconn_t *cptr) {
   if (!cptr || cptr->fd < 0) {
      return;
   }
   size_t len = 0;
   char *p = cptr->sendq;

   // find how much of sendq is complete messages ending with \r\n
   while ((p = strstr(p, "\r\n"))) {
      len = (p - cptr->sendq) + 2;
      p += 2;
   }

   if (len == 0) {
      return;
   }
   ssize_t n = send(cptr->fd, cptr->sendq, len, MSG_DONTWAIT | MSG_NOSIGNAL);

   if (n < 0) {
      if (errno != EAGAIN && errno != EINTR) {
         Log(LOG_CRIT, "irc", "send failed: %s", strerror(errno));
         irc_close_connection(cptr);
      }

      return;
   }

   if (n > 0) {
      rrclient_connection_count_traffic(cptr, 0, (uint64_t) n);
   }
   size_t queued = strlen(cptr->sendq);
   memmove(cptr->sendq, cptr->sendq + n, queued - (size_t)n + 1);

}

bool irc_send(rrconn_t *cptr, const char *fmt, ...) {
   if (!cptr || !fmt) {
      return false;
   }
   char msg[IRC_MSGLEN + 1];
   va_list ap;
   va_start(ap, fmt);
   int written = vsnprintf(msg, sizeof(msg), fmt, ap);
   va_end(ap);

   if (written < 0 || (size_t)written > irc_line_limit(cptr) - 2 || strchr(msg, '\r') || strchr(msg, '\n')) {
      return false;
   }
   size_t msglen = (size_t)written;
#if defined(USE_MONGOOSE)

   if (cptr->conn) {
      if (!cptr->connected || cptr->conn->is_closing || cptr->conn->send.len + msglen + 2 > SENDQLEN) {
         return false;
      }
      msg[msglen++] = '\r';
      msg[msglen++] = '\n';

      bool sent = mg_send(cptr->conn, msg, msglen);

      if (sent) {
         rr_traffic_count(cptr, true, false, msglen);
      }

      return sent;
   }
#endif

   if (cptr->fd < 0) {
      return false;
   }

   if (msglen + 2 + strlen(cptr->sendq) >= SENDQLEN) {
      Log(LOG_WARN, "irc", "sendq full, dropping message");

      return false;
   }
   // append message + CRLF
   size_t cur_len = strlen(cptr->sendq);
   memcpy(cptr->sendq + cur_len, msg, msglen);
   cur_len += msglen;
   cptr->sendq[cur_len++] = '\r';
   cptr->sendq[cur_len++] = '\n';
   cptr->sendq[cur_len] = '\0';
   rr_traffic_count(cptr, true, false, msglen + 2);

   // attempt to send immediately
   if (cptr->connected) {
      irc_try_send(cptr);
   }
   // NB: Any leftover data in the sendq will be flushed by the periodic
   // timer / poll loop calling irc_try_send() again, since we don't have
   // an event loop to watch for writability anymore.

   return cptr->fd >= 0;
}

/*
 * Process incoming data from an IRC connection: read what's available and feed complete lines to irc_process_message(). Called from the poll loop /
 * periodic timer (formerly a libev ev_io callback).
 */
void irc_io_poll(rrconn_t *cptr) {
   irc_capabilities_poll(cptr);

   if (!cptr || cptr->fd < 0 || !cptr->server) {
      return;
   }

   if (!cptr->connected) {
      struct pollfd socket_poll = {
         .fd = cptr->fd, .events = POLLOUT
      };
      int ready = poll(&socket_poll, 1, 0);

      if (ready <= 0) {
         return;
      }
      int error = 0;
      socklen_t length = sizeof(error);

      if (getsockopt(cptr->fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) {
         irc_close_connection(cptr);

         return;
      }
      cptr->connected = now;
   }

   if (!irc_client_register(cptr)) {
      return;
   }

   char buf[IRC_MSGLEN];
   ssize_t n = recv(cptr->fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);

   if (n == 0) {
      irc_close_connection(cptr);

      return;
   }

   if (n < 0) {
      if (errno != EAGAIN && errno != EINTR) {
         Log(LOG_CRIT, "irc", "recv failed: %s", strerror(errno));
         irc_close_connection(cptr);
      }
      // fallthrough: flush any pending sendq
      irc_try_send(cptr);

      return;
   }

   rrclient_connection_count_traffic(cptr, (uint64_t) n, 0);
   irc_receive(cptr, buf, (size_t)n);

   // flush any pending sendq data
   irc_try_send(cptr);
}

bool irc_client_register(rrconn_t *cptr) {
   if (!cptr || !cptr->server) {
      return false;
   }
   bool sent = rustyirc_connection_register(rrclient_irc_state(cptr), cptr->server->ident, cptr->server->account, cptr->server->pass);

   if (!sent) {
      irc_close_connection(cptr);
   }
   cptr->sent_login = sent;

   return sent;
}

void irc_receive(rrconn_t *cptr, const void *data, size_t n) {
   if (!cptr || !data || !n) {
      return;
   }
   cptr->last_heard = now;
   rr_traffic_count(cptr, false, false, n);

   if (!rustyirc_connection_feed(rrclient_irc_state(cptr), data, n)) {
      irc_close_connection(cptr);
   }
}
