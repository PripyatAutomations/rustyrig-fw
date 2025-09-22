//
// au.pipe.c: Pipe/socket interface for audio samples
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we deal with common GUI operations between HMI and framebuffer
//
#include "build_config.h"
#include <librustyaxe/core.h>
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
#include "../ext/libmongoose/mongoose.h"
#include <rrserver/i2c.h>
#include <rrserver/eeprom.h>
#include <rrserver/au.h>
#include <rrserver/au.pipe.h>
#include <librustyaxe/cat.h>
#include <librustyaxe/codecneg.h>
#include <librustyaxe/fwdsp-shared.h>

bool pipe_init(rr_au_pipe_device_t *device, const char *pipe_name) {
   if (!device || !pipe_name) {
      return true;
   }

   device->pipe_fd = open(pipe_name, O_RDWR);
   return device->pipe_fd >= 0;
}

bool pipe_write_samples(rr_au_pipe_device_t *device, const void *samples, size_t size) {
   if (!device || !samples || size <= 0) {
      return true;
   }

   return write(device->pipe_fd, samples, size) == size;
}

bool pipe_read_samples(rr_au_pipe_device_t *device, void *buffer, size_t size) {
   if (!device || !buffer || size <= 0) {
      return true;
   }

   return read(device->pipe_fd, buffer, size) == size;
}

void pipe_cleanup(rr_au_pipe_device_t *device) {
   if (!device) {
      return;
   }

   if (device->pipe_fd >= 0) {
      close(device->pipe_fd);
   }
}

rr_au_backend_interface_t au_backend_pipe = {
    .backend_type = AU_BACKEND_PIPE,
//    .init = pipe_init,
//    .write_samples = pipe_write_samples,
//    .read_samples = pipe_read_samples,
//    .cleanup = pipe_cleanup
};

///////////////////
// Audio Sockets //
///////////////////
static const char *rx_socket_path = DEFAULT_SOCKET_PATH_RX;
static const char *tx_socket_path = DEFAULT_SOCKET_PATH_TX;

int rx_server_fd = -1;
int rx_client_fd = -1;

int setup_rx_unix_socket_server(const char *path) {
   if (!path) {
      return -1;
   }

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

void close_client(void) {
   if (rx_client_fd >= 0) {
      close(rx_client_fd);
      rx_client_fd = -1;
   }
}

void close_server(void ) {
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
// XXX: We need to find the channel ID associated with the connection
// XXX: Then send it using void au_send_to_ws(const void *data, size_t len, int channel) {
// XXX: Dead code
//      Log(LOG_DEBUG, "audio", "Read %zd bytes from UNIX socket client (fd=%d)", n, rx_client_fd);
//      broadcast_audio_to_ws_clients(buf, n);
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
