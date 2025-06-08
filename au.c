//
// au.c: Handle receiving/sending audio between this service and audio backends such as fwdsp for gstreamer
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we wrap around our supported audio interfaces
//
// Most of the ugly bits should go in the per-backend sources
//
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <sys/un.h>
#include <fcntl.h>
#include "inc/i2c.h"
#include "inc/state.h"
#include "inc/eeprom.h"
#include "inc/logger.h"
#include "inc/cat.h"
#include "inc/posix.h"
#include "inc/au.h"
#include "inc/au.pipe.h"
#include "inc/ws.h"
#include "inc/fwdsp-shared.h"

// XXX: This needs moved to config/${profile}.fwdsp.json:fwdsp.channels.name['rx'].path

rr_au_backend_interface_t au_backend_null = {
    .backend_type = AU_BACKEND_NULL_SINK,
    .init = NULL,
    .write_samples = NULL,
    .read_samples = NULL,
    .cleanup = NULL
};

bool rr_au_init(void) {
    rr_au_backend_interface_t *be = &au_backend_null;

    // Initialize the selected backend
    if (be && be->init) {
        return be->init();
    }
    return true;
}

bool rr_au_write_samples(rr_au_backend_interface_t *be ,const void *samples, size_t size) {
    if (be->write_samples) {
        return be->write_samples(samples, size);
    }
    return false;
}

rr_au_sample_t **rr_au_read_samples(rr_au_backend_interface_t *be) {
    if (be->read_samples) {
        return be->read_samples();
    }
    return NULL;
}

void rr_au_cleanup(rr_au_backend_interface_t *be, rr_au_device_t *dev) {
    if (be->cleanup) {
        be->cleanup(dev);
    }
}

///////////////////
// Audio Sockets //
///////////////////
static const char *rx_socket_path = DEFAULT_SOCKET_PATH_RX;
static const char *tx_socket_path = DEFAULT_SOCKET_PATH_TX;

int rx_server_fd = -1;
int rx_client_fd = -1;

int setup_rx_unix_socket_server(const char *path) {
   struct sockaddr_un addr = {0};
   int fd;

   fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (fd < 0) {
      perror("socket");
      return -1;
   }

   unlink(path);

   addr.sun_family = AF_UNIX;
   strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

   if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      perror("bind");
      close(fd);
      return -1;
   }

   if (listen(fd, 5) < 0) {
      perror("listen");
      close(fd);
      return -1;
   }

   // Make the socket non-blocking
   if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
      perror("fcntl");
      close(fd);
      return -1;
   }

   Log(LOG_DEBUG, "au", "UNIX socket server listening on %s", path);
   return fd;
}

void close_client() {
   if (rx_client_fd >= 0) {
      close(rx_client_fd);
      rx_client_fd = -1;
   }
}

void close_server() {
   if (rx_server_fd >= 0) {
      close(rx_server_fd);
      rx_server_fd = -1;
      unlink(rx_socket_path);
   }
}

void au_unix_socket_init(void) {
   rx_server_fd = setup_rx_unix_socket_server(DEFAULT_SOCKET_PATH_RX);

   if (rx_server_fd < 0) {
      Log(LOG_DEBUG, "au", "Failed to create UNIX server socket");
   }
}

void au_unix_socket_cleanup(void) {
   close_client();
   close_server();
}

void au_unix_socket_poll(void) {
   // Accept new connections if any
   int client_fd = accept(rx_server_fd, NULL, NULL);

   if (client_fd >= 0) {
      Log(LOG_INFO, "audio", "fwdsp connected on UNIX socket (fd=%d)", client_fd);

      // Set non-blocking for client socket
      fcntl(client_fd, F_SETFL, O_NONBLOCK);
      rx_client_fd = client_fd;
      return; // accept one connection per poll, or loop if you want multiple
   } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      perror("accept");
   }

   if (rx_client_fd < 0) {
      // No connected client
      return;
   }

   // Read from the client socket
   uint8_t buf[800];
   ssize_t n = read(rx_client_fd, buf, sizeof(buf));

   if (n > 0) {
//      Log(LOG_DEBUG, "audio", "Read %zd bytes from UNIX socket client (fd=%d)", n, rx_client_fd);
      broadcast_audio_to_ws_clients(buf, n);
   } else if (n == 0) {
      // Client closed connection
      Log(LOG_INFO, "au", "fwdsp disconnected (fd=%d)", rx_client_fd);
      close(rx_client_fd);
      rx_client_fd = -1;
   } else if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
         // No data available, not an error
         return;
      }
      // Real error reading
      Log(LOG_WARN, "au", "fwdsp read error on UNIX socket client (fd=%d), closing. error %d:%s", rx_client_fd, errno, strerror(errno));
      close(rx_client_fd);
      rx_client_fd = -1;
   }
}

static bool parse_format_header(int fd, struct audio_header *hdr_out) {
   ssize_t r;
   size_t total = 0;
   uint8_t *buf = (uint8_t *)hdr_out;

   while (total < sizeof(*hdr_out)) {
      r = read(fd, buf + total, sizeof(*hdr_out) - total);
      if (r <= 0) {
         return false;
      }
      total += r;
   }

   if (hdr_out->magic[0] != 'A' || hdr_out->magic[1] != 'U') {
      Log(LOG_DEBUG, "au", "Invalid header magic");
      return false;
   }

   return true;
}
