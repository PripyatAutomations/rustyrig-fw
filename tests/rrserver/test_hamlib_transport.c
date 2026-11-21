// SPDX-License-Identifier: GPL-2.0-only
/* Exercise real fork/exec, private fd 3, heartbeat, request/reply and exit
 * without Hamlib or a physical radio. The test binary doubles as the child.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "../../rrserver/hamlib.ipc.h"
#include "../../rrserver/hamlib.transport.h"

static int send_test(int fd, uint16_t type, uint32_t seq, const void *data, size_t size) {
   uint8_t packet[RR_HAMLIB_IPC_HEADER_SIZE + RR_HAMLIB_IPC_MAX_PAYLOAD] = {0};
   rr_hamlib_ipc_header_t h = { .magic = RR_HAMLIB_IPC_MAGIC,
      .version = RR_HAMLIB_IPC_VERSION, .type = type, .sequence = seq,
      .payload_size = (uint32_t)size };
   if (!rr_hamlib_ipc_encode(packet, sizeof(packet), &h)) return -1;
   if (size) memcpy(packet + RR_HAMLIB_IPC_HEADER_SIZE, data, size);
   return send(fd, packet, RR_HAMLIB_IPC_HEADER_SIZE + size, MSG_NOSIGNAL)
      == (ssize_t)(RR_HAMLIB_IPC_HEADER_SIZE + size) ? 0 : -1;
}
static int fake_worker(int argc, char **argv) {
   if (argc != 11 || strcmp(argv[1], "--parent") || strcmp(argv[3], "--model") ||
       strcmp(argv[5], "--device") || strcmp(argv[7], "--baud") ||
       strcmp(argv[9], "--timeout-ms")) return 2;
   if (getppid() != (pid_t)atoi(argv[2])) return 3;
   const int fd = 3;
   if (send_test(fd, RR_HAMLIB_IPC_HELLO, 0, NULL, 0)) return 4;
   uint8_t packet[RR_HAMLIB_IPC_HEADER_SIZE + RR_HAMLIB_IPC_MAX_PAYLOAD];
   rr_hamlib_ipc_header_t h;
   bool accepted = false;
   bool idle_ping_answered = false;
   unsigned releases = 0;
   for (;;) {
      struct pollfd pfd = { .fd = fd, .events = POLLIN };
      if (poll(&pfd, 1, 4000) <= 0) return 5;
      if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return 0;
      ssize_t size = recv(fd, packet, sizeof(packet), 0);
      if (size <= 0) return 0;
      if (!rr_hamlib_ipc_decode(&h, packet, (size_t)size)) return 6;
      if (!accepted) {
         if (h.type != RR_HAMLIB_IPC_HELLO_ACK || h.sequence || h.payload_size)
            return 7;
         accepted = true;
         continue;
      }
      if (h.type == RR_HAMLIB_IPC_HEARTBEAT) continue;
      if (h.type == RR_HAMLIB_IPC_PONG && h.sequence == 444 &&
          h.payload_size == 0) {
         idle_ping_answered = true;
         continue;
      }
      if (h.type == RR_HAMLIB_IPC_RELEASE && h.payload_size == 0) {
         if (++releases == 1) {
            /* Demand a PONG in the middle of a synchronous exchange.
             * A server that blindly waits for RELEASE_RESULT will deadlock.
             */
            if (send_test(fd, RR_HAMLIB_IPC_PING, 333, NULL, 0)) return 10;
            bool matched = false;
            for (int tries = 0; tries < 30 && !matched; ++tries) {
               struct pollfd wait = { .fd = fd, .events = POLLIN };
               if (poll(&wait, 1, 100) <= 0) continue;
               ssize_t n = recv(fd, packet, sizeof(packet), 0);
               rr_hamlib_ipc_header_t response;
               if (n <= 0 || !rr_hamlib_ipc_decode(&response, packet, (size_t)n))
                  return 11;
               if (response.type == RR_HAMLIB_IPC_HEARTBEAT) continue;
               matched = response.type == RR_HAMLIB_IPC_PONG &&
                  response.sequence == 333 && response.payload_size == 0;
               if (!matched) return 12;
            }
            if (!matched) return 13;
         } else if (!idle_ping_answered) {
            return 14;
         }
         uint8_t released = 0;
         if (send_test(fd, RR_HAMLIB_IPC_RELEASE_RESULT, h.sequence,
               &released, sizeof(released))) return 8;
         if (releases == 1 && send_test(fd, RR_HAMLIB_IPC_PING, 444, NULL, 0))
            return 15;
         continue;
      }
      return 9;
   }
}
int main(int argc, char **argv) {
   if (argc > 1) return fake_worker(argc, argv);
   char *path = realpath(argv[0], NULL);
   assert(path);
   rr_hamlib_transport_t t;
   assert(rr_hamlib_transport_open(&t, path, 2, "fake", 38400, 250));
   assert(rr_hamlib_transport_ready(&t));
   uint8_t result = 99;
   uint32_t size = 0;
   assert(rr_hamlib_transport_exchange(&t, RR_HAMLIB_IPC_RELEASE, NULL, 0,
      RR_HAMLIB_IPC_RELEASE_RESULT, &result, &size, sizeof(result), 2000));
   assert(size == 1 && result == 0);
   /* Drive the exact same idle-service callback as backend.worker. */
   for (int i = 0; i < 6; ++i) {
      usleep(100000);
      assert(rr_hamlib_transport_service(&t));
   }
   assert(rr_hamlib_transport_exchange(&t, RR_HAMLIB_IPC_RELEASE, NULL, 0,
      RR_HAMLIB_IPC_RELEASE_RESULT, &result, &size, sizeof(result), 2000));
   assert(size == 1 && result == 0);
   rr_hamlib_transport_close(&t);
   free(path);
   return 0;
}
