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
   close(fd[0]);
   close(fd[1]);
   return 0;
}
