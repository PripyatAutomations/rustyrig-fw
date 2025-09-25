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
      Log(LOG_CRIT, "irc", "irc_cli_connect called with NULL srv!");
      return NULL;
   }

   irc_client_t *cptr = calloc(1, sizeof(*cptr));
   if (!cptr) {
      fprintf(stderr, "OOM in irc_cli_connect\n");
      return NULL;
   }
   cptr->server = srv;
   snprintf(cptr->nick, sizeof(cptr->nick), "%s", srv->nick[0] ? srv->nick : "nonick");

   // Resolve and connect
   struct addrinfo hints, *res, *rp;
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;

   char portbuf[16];
   snprintf(portbuf, sizeof(portbuf), "%d", srv->port);

   int gai = getaddrinfo(srv->host, portbuf, &hints, &res);
   if (gai != 0) {
      Log(LOG_CRIT, "irc", "getaddrinfo(%s:%s): %s",
          srv->host, portbuf, gai_strerror(gai));
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
      break; // success or in-progress
   }
   freeaddrinfo(res);

   if (fd == -1) {
      Log(LOG_CRIT, "irc", "could not connect to %s:%d",
          srv->host, srv->port);
      free(cptr);
      return NULL;
   }

   cptr->fd = fd;

   // Register fd with libev
   ev_io_init(&cptr->io_watcher, irc_io_cb, fd, EV_WRITE | EV_READ);
   ev_io_start(EV_DEFAULT, &cptr->io_watcher);

   Log(LOG_INFO, "irc", "connecting to %s:%d (fd=%d)",
       srv->host, srv->port, fd);

   return cptr;
}

static void irc_io_cb(EV_P_ ev_io *w, int revents) {
   irc_client_t *cptr = (irc_client_t *)(((char*)w) - offsetof(irc_client_t, io_watcher));

   if (revents & EV_WRITE) {
      // connection established
      Log(LOG_INFO, "irc", "connected to %s:%d", cptr->server->host, cptr->server->port);

      // Stop watching for write
      ev_io_set(w, cptr->fd, EV_READ);

      // Send initial handshake
      char buf[256];
      snprintf(buf, sizeof(buf), "NICK %s\r\nUSER %s 0 * :%s\r\n",
               cptr->nick, cptr->nick, cptr->nick);
      send(cptr->fd, buf, strlen(buf), 0);
   }

   if (revents & EV_READ) {
      char buf[512];
      ssize_t n = recv(cptr->fd, buf, sizeof(buf)-1, 0);
      if (n <= 0) {
         Log(LOG_INFO, "irc", "disconnected");
         ev_io_stop(EV_A_ w);
         close(cptr->fd);
         return;
      }
      buf[n] = '\0';
      Log(LOG_DEBUG, "irc", "recv: %s", buf);

      // TODO: pass to IRC parser
   }
}
