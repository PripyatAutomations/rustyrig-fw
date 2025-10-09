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
#include <ev.h>

static void irc_io_cb(EV_P_ ev_io *w, int revents);

irc_client_t *irc_cli_connect(server_cfg_t *srv) {
   if (!srv) {
      return NULL;
   }

   irc_client_t *cptr = calloc(1, sizeof(*cptr));
   if (!cptr) {
      return NULL;
   }

   cptr->server = srv;
   snprintf(cptr->nick, sizeof(cptr->nick), "%s", srv->nick[0] ? srv->nick : "nonick");

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
   for (rp = res; rp != NULL; rp = rp->ai_next) {
      fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (fd == -1) continue;

      fcntl(fd, F_SETFL, O_NONBLOCK);

      int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
      if (rc < 0 && errno != EINPROGRESS) {
         close(fd);
         fd = -1;
         continue;
      }
      break;
   }
   freeaddrinfo(res);

   if (fd == -1) {
      free(cptr);
      return NULL;
   }

   cptr->fd = fd;

   if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
      // Immediate connection
      cptr->connected = true;
      ev_io_init(&cptr->io_watcher, irc_io_cb, fd, EV_READ);
      ev_io_start(EV_DEFAULT, &cptr->io_watcher);

      // Send login
      if (cptr->server->pass[0]) {
         if (cptr->server->account[0])
            irc_send(cptr, "PASS %s:%s", cptr->server->account, cptr->server->pass);
         else
            irc_send(cptr, "PASS %s", cptr->server->pass);
      }
      irc_send(cptr, "NICK %s", cptr->nick);
      const char *ident = cptr->server->ident[0] ? cptr->server->ident : cptr->nick;
      irc_send(cptr, "USER %s 0 * :%s", ident, cptr->nick);
   } else {
      // Connection in progress
      ev_io_init(&cptr->io_watcher, irc_io_cb, fd, EV_WRITE);
      ev_io_start(EV_DEFAULT, &cptr->io_watcher);
   }

   return cptr;
}

static void irc_io_cb(EV_P_ ev_io *w, int revents) {
   irc_client_t *cptr = (irc_client_t *)(((char*)w) - offsetof(irc_client_t, io_watcher));

   if (revents & EV_WRITE && !cptr->connected) {
      int err = 0;
      socklen_t len = sizeof(err);
      getsockopt(cptr->fd, SOL_SOCKET, SO_ERROR, &err, &len);
      if (err != 0) {
         ev_io_stop(EV_A_ w);
         close(cptr->fd);
         cptr->connected = false;
         return;
      }

      // Connected!
      cptr->connected = true;
      ev_io_set(w, cptr->fd, EV_READ);
      ev_io_start(EV_A_ w);

      // Send login
      if (cptr->server->pass[0]) {
         if (cptr->server->account[0])
            irc_send(cptr, "PASS %s:%s", cptr->server->account, cptr->server->pass);
         else
            irc_send(cptr, "PASS %s", cptr->server->pass);
      }
      irc_send(cptr, "NICK %s", cptr->nick);
      const char *ident = cptr->server->ident[0] ? cptr->server->ident : cptr->nick;
      irc_send(cptr, "USER %s 0 * :%s", ident, cptr->nick);
   }

   if (revents & EV_READ) {
      char buf[512];
      ssize_t n = recv(cptr->fd, buf, sizeof(buf), 0);
      if (n <= 0) {
         ev_io_stop(EV_A_ w);
         close(cptr->fd);
         cptr->connected = false;
         return;
      }

      size_t cur_len = strlen(cptr->recvq);
      if ((size_t)n + cur_len >= RECVQLEN) {
         cptr->recvq[0] = '\0';
         cur_len = 0;
      }
      memcpy(cptr->recvq + cur_len, buf, n);
      cptr->recvq[cur_len + n] = '\0';

      char *line;
      while ((line = strstr(cptr->recvq, "\r\n"))) {
         *line = '\0';
         irc_process_message(cptr, cptr->recvq);
         size_t rem = strlen(line + 2);
         memmove(cptr->recvq, line + 2, rem + 1);
      }
   }
}
