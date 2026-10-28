/* Dedicated development socket for the future OpenSSL/wslay mesh
 * transport. Never accept unauthenticated application data here. */
#include "mesh.native.listener.h"
#include "reactor.glib.h"
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
   for (;;) {
      int incoming = accept(fd, NULL, NULL);
      if (incoming < 0) {
         if (errno == EAGAIN || errno == EWOULDBLOCK) break;
         if (errno == EINTR) continue;
         Log(LOG_WARN, "mesh.native", "Native mesh accept failed: %s", g_strerror(errno));
         break;
      }
      /* Refuse connections until the TLS/WebSocket driver is wired in.
       * This socket must not become a plaintext fallback. */
      close(incoming);
   }
   return true;
}

bool rr_mesh_native_listener_start(void) {
   if (!cfg_get_bool("links.native.enabled", false)) return true;
   if (native_fd >= 0) return true;
   const char *bind_ip = cfg_get("links.native.bind");
   if (!bind_ip || !*bind_ip) bind_ip = "0.0.0.0";
   int port = cfg_get_int("links.native.port", 4421);
   struct sockaddr_in addr = { .sin_family = AF_INET };
   if (port < 1 || port > 65535 ||
       inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
      Log(LOG_CRIT, "mesh.native", "Invalid links.native.bind/port; expected IPv4 and port 1-65535");
      return false;
   }
   addr.sin_port = htons((uint16_t)port);
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   if (fd < 0) return false;
   int flags = fcntl(fd, F_GETFL, 0);
   int one = 1;
   if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
       setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0 ||
       bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
       listen(fd, 32) < 0) {
      Log(LOG_CRIT, "mesh.native", "Unable to bind %s:%d: %s", bind_ip, port, g_strerror(errno));
      close(fd);
      return false;
   }
   native_watch = rr_reactor_watch_fd(NULL, fd, G_IO_IN, native_ready, NULL);
   if (!native_watch) { close(fd); return false; }
   native_fd = fd;
   Log(LOG_WARN, "mesh.native", "Development listener on %s:%d (TCP); TLS/WSS handshake not yet installed; connections rejected", bind_ip, port);
   return true;
}

void rr_mesh_native_listener_stop(void) {
   if (native_watch) {
      rr_reactor_watch_cancel(native_watch);
      native_watch = NULL;
   }
   if (native_fd >= 0) { close(native_fd); native_fd = -1; }
}
