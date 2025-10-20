//
// irc.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

bool irc_init(void) {
   // XXX: These need to go into the irc_init() or irc_client_init/irc_server_init functions as appropriate!
   irc_register_default_callbacks();
   irc_register_default_numeric_callbacks();
   return false;
}

//
// This will create a dict containing various things which we'll allow substituting
dict *irc_generate_vars(irc_conn_t *cptr, const char *chan) {
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

static void irc_try_send(irc_conn_t *cptr) {
   if (!cptr || cptr->fd <= 0) {
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

   ssize_t n = send(cptr->fd, cptr->sendq, len, 0);
   Log(LOG_CRIT, "irc", "send(%d) to cptr:<%p>: %d bytes: %.*s",
       cptr->fd, cptr, (int)n, (int)len, cptr->sendq);

   if (n < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
         Log(LOG_CRIT, "irc", "send failed: %s", strerror(errno));
         close(cptr->fd);
         cptr->connected = false;
      }
      return;
   }

   if ((size_t)n < len) {
      // partial send, move remaining to front
      memmove(cptr->sendq, cptr->sendq + n, len - n);
      cptr->sendq[len - n] = '\0';
   } else {
      // full send, move any leftover queued messages
      size_t remaining = strlen(cptr->sendq + len);
      if (remaining > 0) {
         memmove(cptr->sendq, cptr->sendq + len, remaining + 1);
      } else {
         cptr->sendq[0] = '\0';
      }
   }
}

bool irc_send(irc_conn_t *cptr, const char *fmt, ...) {
   if (!cptr || !fmt || cptr->fd <= 0) {
      return false;
   }

   char msg[512];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(msg, sizeof(msg), fmt, ap);
   va_end(ap);

   size_t msglen = strlen(msg);
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

   // attempt to send immediately
   irc_try_send(cptr);

   // watch for EV_WRITE only if there’s still data
   if (cptr->sendq[0] != '\0') {
      ev_io_set(&cptr->io_watcher, cptr->fd, EV_READ | EV_WRITE);
      ev_io_start(EV_DEFAULT, &cptr->io_watcher);
   } else {
      ev_io_set(&cptr->io_watcher, cptr->fd, EV_READ);
   }

   return true;
}

void irc_io_cb(EV_P_ ev_io *w, int revents) {
   irc_conn_t *cptr = (irc_conn_t *)(((char*)w) - offsetof(irc_conn_t, io_watcher));

   if (revents & EV_READ) {
      char buf[512];
      ssize_t n = recv(cptr->fd, buf, sizeof(buf), 0);
      if (n <= 0) {
         ev_io_stop(EV_A_ w);
         close(cptr->fd);
         cptr->connected = false;
         return;
      }

      buf[n] = '\0';

      // append to recvq safely
      size_t cur_len = strlen(cptr->recvq);
      if (cur_len + n >= RECVQLEN) {
         Log(LOG_WARN, "irc", "recvq overflow, resetting");
         cptr->recvq[0] = '\0';
         cur_len = 0;
      }
      memcpy(cptr->recvq + cur_len, buf, n);
      cur_len += n;
      cptr->recvq[cur_len] = '\0';

      // process complete lines
      char *start = cptr->recvq;
      char *end;
      while ((end = strstr(start, "\r\n"))) {
         *end = '\0';
         Log(LOG_DEBUG, "net", "processing line: [%s]", start);
         irc_process_message(cptr, start);

         // send login on first server message
         if (!cptr->sent_login) {
            tui_print_win(tui_active_window(), "[{green}%s{reset}] {bright-cyan}***{reset} Sending login {bright-cyan}***{reset} ");

            if (cptr->server->pass[0]) {
               if (cptr->server->account[0])
                  irc_send(cptr, "PASS %s:%s", cptr->server->account, cptr->server->pass);
               else
                  irc_send(cptr, "PASS %s", cptr->server->pass);
            }
            irc_send(cptr, "NICK %s", cptr->nick);
            const char *ident = cptr->server->ident[0] ? cptr->server->ident : cptr->nick;
            irc_send(cptr, "USER %s 0 * :%s", ident, cptr->nick);
            cptr->sent_login = true;
         }

         start = end + 2;
      }

      // move leftover partial line to front
      size_t leftover = strlen(start);
      memmove(cptr->recvq, start, leftover + 1);
   }

   if (revents & EV_WRITE) {
      irc_try_send(cptr);
      if (strchr(cptr->sendq, '\r') == NULL) {
         ev_io_set(EV_A_ w, cptr->fd, EV_READ);
      }
   }
}
