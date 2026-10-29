//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Dedicated authenticated mesh WSS listener, separate from client HTTPS. */
#include "mesh.native.listener.h"
#include "mesh.wss.h"
#include "link.h"
#include "mesh.native.client.h"
#include "mesh.enroll.native.h"
#include <librustyaxe/reactor.glib.h>
#include <librustyaxe/core.h>
#include <glib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

static int native_fd = -1;
static rr_reactor_watch_t *native_watch;

static bool native_ready(int fd, GIOCondition events, void *ctx) {
   (void)ctx;

   if (events & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
      Log(LOG_WARN, "mesh.native", "Native mesh listener socket error");
      native_watch = NULL;
      native_fd = -1;
      close(fd);

      return false;
   }

   for (unsigned accepted = 0 ; accepted < 32 ; ++accepted) {
      int incoming = accept(fd, NULL, NULL);

      if (incoming < 0) {
         if (errno == EAGAIN) {
            break;
         }
#if EWOULDBLOCK != EAGAIN

         if (errno == EWOULDBLOCK) {
            break;
         }
#endif

         if (errno == EINTR) {
            continue;
         }
         Log(LOG_WARN, "mesh.native", "Native mesh accept failed: %s", g_strerror(errno));
         break;
      }
      rrserver_native_accept(incoming);
   }

   return true;
}

bool rr_mesh_native_listener_start(void) {
   rr_mesh_wss_http_handler(rr_mesh_native_enrollment);

   if (!rr_mesh_wss_init(cfg_get("net.http.tls-cert"), cfg_get("net.http.tls-key"), rrserver_links_native_pin)) {
      Log(LOG_CRIT, "mesh.native", "Cannot initialize OpenSSL mesh transport with HTTPS identity certificate/key");

      return false;
   }

   if (!cfg_get_bool("links.native.enabled", true)) {
      return true;
   }

   if (native_fd >= 0) {
      return true;
   }
   const char *bind_ip = cfg_get("links.native.bind");

   if (!bind_ip || !*bind_ip) {
      bind_ip = "0.0.0.0";
   }
   int port = cfg_get_int("net.http.tls-port", 4420);
   struct sockaddr_in addr = {
      .sin_family = AF_INET
   };

   if (port < 1 || port > 65535 || inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
      Log(LOG_CRIT, "mesh.native", "Invalid links.native.bind/net.http.tls-port; expected IPv4 and port 1-65535");

      return false;
   }
   addr.sin_port = htons((uint16_t) port);
   int fd = socket(AF_INET, SOCK_STREAM, 0);

   if (fd < 0) {
      return false;
   }
   int flags = fcntl(fd, F_GETFL, 0);
   int one = 1;

   if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
      setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 32) < 0) {
      Log(LOG_CRIT, "mesh.native", "Unable to bind %s:%d: %s", bind_ip, port, g_strerror(errno));
      close(fd);

      return false;
   }
   native_watch = rr_reactor_watch_fd(NULL, fd, G_IO_IN, native_ready, NULL);

   if (!native_watch) {
      close(fd);

      return false;
   }
   native_fd = fd;
   Log(LOG_INFO, "mesh.native", "Native TLS listener on %s:%d (mesh /link, client /ws, mesh enrollment)", bind_ip, port);

   return true;
}

void rr_mesh_native_listener_stop(void) {
   rrserver_links_native_stopping();

   if (native_watch) {
      rr_reactor_watch_cancel(native_watch);
      native_watch = NULL;
   }

   if (native_fd >= 0) {
      close(native_fd);
      native_fd = -1;
   }
   rr_mesh_wss_stop();
}
