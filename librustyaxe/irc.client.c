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

void irc_io_cb(EV_P_ ev_io *w, int revents);

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
      if (fd == -1) {
         continue;
      }

      fcntl(fd, F_SETFL, O_NONBLOCK);

      int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
      Log(LOG_DEBUG, "irc.net", "connect with fd: %d rc=%d errno=%d", fd, rc, errno);

      if (rc == 0) {
         cptr->connected = true;
         break;
      } else if (errno == EINPROGRESS) {
         cptr->connected = false;
         break;
      } else {
         close(fd);
         fd = -1;
      }
   }
   freeaddrinfo(res);

   if (fd == -1) {
      free(cptr);
      return NULL;
   }

   cptr->fd = fd;

   if (cptr->connected) {
      ev_io_init(&cptr->io_watcher, irc_io_cb, fd, EV_READ);
      ev_io_start(EV_DEFAULT, &cptr->io_watcher);

      if (srv->pass[0]) {
         Log(LOG_CRIT, "irc", "calling send auth from %s()", __FUNCTION__);
         if (srv->account[0]) {
            irc_send(cptr, "PASS %s:%s", srv->account, srv->pass);
         } else {
            irc_send(cptr, "PASS %s", srv->pass);
         }
      }

      const char *ident = srv->ident[0] ? srv->ident : cptr->nick;
      irc_send(cptr, "NICK %s", cptr->nick);
      irc_send(cptr, "USER %s 0 * :%s", ident, cptr->nick);
   } else {
      ev_io_init(&cptr->io_watcher, irc_io_cb, fd, EV_WRITE);
      ev_io_start(EV_DEFAULT, &cptr->io_watcher);
   }

   return cptr;
}
