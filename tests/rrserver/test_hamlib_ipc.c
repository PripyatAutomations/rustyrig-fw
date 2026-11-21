// SPDX-License-Identifier: GPL-2.0-only
/* Standalone protocol regression test, no rig or Hamlib required. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../../rrserver/hamlib.ipc.h"

int main(void) {
   int fd[2];
   assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fd) == 0);
   uint8_t wire[RR_HAMLIB_IPC_HEADER_SIZE + 3] = {0};
   rr_hamlib_ipc_header_t h = {
      .magic = RR_HAMLIB_IPC_MAGIC, .version = RR_HAMLIB_IPC_VERSION,
      .type = RR_HAMLIB_IPC_HELLO, .sequence = 0x12345678,
      .payload_size = 3
   }, got;
   assert(rr_hamlib_ipc_encode(wire, sizeof(wire), &h));
   memcpy(wire + RR_HAMLIB_IPC_HEADER_SIZE, "abc", 3);
   assert(send(fd[0], wire, sizeof(wire), 0) == (ssize_t)sizeof(wire));
   uint8_t received[sizeof(wire)];
   assert(recv(fd[1], received, sizeof(received), 0) == (ssize_t)sizeof(received));
   assert(rr_hamlib_ipc_decode(&got, received, sizeof(received)));
   assert(got.sequence == h.sequence && got.type == h.type);
   assert(memcmp(received + RR_HAMLIB_IPC_HEADER_SIZE, "abc", 3) == 0);
   assert(!rr_hamlib_ipc_decode(&got, received, sizeof(received)-1));
   received[5]++;
   assert(!rr_hamlib_ipc_decode(&got, received, sizeof(received)));
   received[5]--;
   received[15] = 255;
   assert(!rr_hamlib_ipc_decode(&got, received, sizeof(received)));
   received[15] = 3;
   received[7] = 0xff;
   assert(!rr_hamlib_ipc_decode(&got, received, sizeof(received)));
   /* Required in-band control liveness messages are version 2 frames. */
   uint8_t probe[RR_HAMLIB_IPC_HEADER_SIZE] = {0};
   rr_hamlib_ipc_header_t ping = {
      .magic = RR_HAMLIB_IPC_MAGIC, .version = RR_HAMLIB_IPC_VERSION,
      .type = RR_HAMLIB_IPC_PING, .sequence = 42, .payload_size = 0
   };
   assert(rr_hamlib_ipc_encode(probe, sizeof(probe), &ping));
   assert(rr_hamlib_ipc_decode(&got, probe, sizeof(probe)));
   assert(got.type == RR_HAMLIB_IPC_PING && got.sequence == 42);
   ping.type = RR_HAMLIB_IPC_PONG;
   assert(rr_hamlib_ipc_encode(probe, sizeof(probe), &ping));
   assert(rr_hamlib_ipc_decode(&got, probe, sizeof(probe)));
   assert(got.type == RR_HAMLIB_IPC_PONG && got.sequence == 42);
   probe[5] = 1; /* obsolete version 1 must be refused */
   assert(!rr_hamlib_ipc_decode(&got, probe, sizeof(probe)));

   /* Application request encoding has no native C struct ABI dependency. */
   uint8_t command[RR_HAMLIB_IPC_COMMAND_SIZE] = {0};
   command[0] = RR_HAMLIB_OP_FREQ_SET;
   command[1] = 1;
   rr_hamlib_ipc_put32(command + 4, 14074000);
   assert(rr_hamlib_ipc_get32(command + 4) == 14074000);
   assert(rr_hamlib_ipc_get32(command + 8) == 0);

   uint8_t response[RR_HAMLIB_IPC_RESULT_SIZE] = {0};
   rr_hamlib_ipc_put32(response + 4, 7074000);
   rr_hamlib_ipc_put32(response + 8, RR_HAMLIB_MODE_LSB);
   rr_hamlib_ipc_put32(response + 12, 2400);
   assert(rr_hamlib_ipc_get32(response + 4) == 7074000);
   assert(rr_hamlib_ipc_get32(response + 8) == RR_HAMLIB_MODE_LSB);
   assert(rr_hamlib_ipc_get32(response + 12) == 2400);

   close(fd[0]);
   close(fd[1]);
   return 0;
}
