/* Live test peer, using the same socket and packet adapter as the native client. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <unistd.h>
#include <sys/select.h>
#include <librrprotocol/rtp.socket.h>
static bool received(void *user, const uint8_t *frame, size_t length) {
   (void)user;
   if (!frame)
      return true;
   uint32_t hash = 2166136261;
   for (size_t i = 28; i < length; i++)
      hash = (hash ^ frame[i]) * 16777619;
   printf("FRAME %zu %u\n", length, hash);
   fflush(stdout);
   return true;
}
int main(int argc, char **argv) {
   if (argc == 2 && !strcmp(argv[1], "--supported"))
      return rr_rtp_supported(false) ? 0 : 1;
   char endpoint[320], hex[61];
   unsigned ssrc, encrypted;
   if (scanf("%319s %60s %u %u", endpoint, hex, &ssrc, &encrypted) != 4)
      return 2;
   uint8_t key[30];
   for (unsigned i = 0; i < 30; i++) {
      unsigned byte;
      if (sscanf(hex + i * 2, "%2x", &byte) != 1)
         return 2;
      key[i] = byte;
   }
   rr_rtp_socket_t *socket = rr_rtp_socket_open(NULL, 0);
   rr_rtp_attachment_t *peer = rr_rtp_socket_attach(socket, ssrc, key, encrypted, endpoint, NULL);
   if (!peer)
      return 3;
   bool ready = false;
   uint64_t end = mono_us() + 30000000;
   while ((uint64_t) mono_us() < end) {
      rr_rtp_socket_poll(socket, mono_us(), NULL, received);
      if (!ready && rr_rtp_socket_active(peer, mono_us())) {
         puts("READY");
         fflush(stdout);
         ready = true;
      }
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(STDIN_FILENO, &fds);
      struct timeval timeout = {.tv_usec = 10000 };
      if (select(1, &fds, NULL, NULL, &timeout) > 0) {
         char line[512];
         if (!fgets(line, sizeof(line), stdin))
            break;
         if (!strncmp(line, "SEND ", 5)) {
            char *path = line + 5;
            path[strcspn(path, "\r\n")] = 0;
            FILE *f = fopen(path, "rb");
            if (!f)
               return 4;
            uint8_t data[65535];
            size_t length = fread(data, 1, sizeof(data), f);
            fclose(f);
            printf("SENT %d\n", rr_rtp_socket_send(peer, data, length, mono_us()));
            fflush(stdout);
         }
      }
   }
   rr_rtp_socket_close(socket);
   return 0;
}
