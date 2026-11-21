// SPDX-License-Identifier: GPL-2.0-only
/*
 * rrhamlib: private per-radio Hamlib process.
 * Phase 1 runtime: read-only protocol and verified startup key-up.
 * TX is deliberately unsupported until independent lease enforcement and
 * failure injection are complete. Never run alongside another CAT owner.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <hamlib/rig.h>
#include "hamlib.ipc.h"

enum { WORKER_FD = 3, MAX_PACKET = RR_HAMLIB_IPC_HEADER_SIZE + RR_HAMLIB_IPC_MAX_PAYLOAD };
static volatile sig_atomic_t quitting;
static void stop_signal(int sig) { (void)sig; quitting = 1; }
static int64_t monotonic_ms(void) {
   struct timespec now;
   if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
   return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static int send_packet(int fd, uint16_t type, uint32_t sequence, const uint8_t *payload, uint32_t len) {
   if (len > RR_HAMLIB_IPC_MAX_PAYLOAD) return -1;
   uint8_t packet[MAX_PACKET];
   rr_hamlib_ipc_header_t h = { .magic = RR_HAMLIB_IPC_MAGIC, .version = RR_HAMLIB_IPC_VERSION,
      .type = type, .sequence = sequence, .payload_size = len };
   if (!rr_hamlib_ipc_encode(packet, sizeof(packet), &h)) return -1;
   if (len && payload) memcpy(packet + RR_HAMLIB_IPC_HEADER_SIZE, payload, len);
   ssize_t sent = send(fd, packet, RR_HAMLIB_IPC_HEADER_SIZE + len, MSG_NOSIGNAL | MSG_DONTWAIT);
   return sent == (ssize_t)(RR_HAMLIB_IPC_HEADER_SIZE + len) ? 0 : -1;
}
static int receive_packet(int fd, rr_hamlib_ipc_header_t *h, uint8_t *packet, size_t cap) {
   struct iovec iov = { .iov_base = packet, .iov_len = cap };
   struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
   ssize_t got = recvmsg(fd, &msg, MSG_DONTWAIT);
   if (got <= 0 || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) return -1;
   if (!rr_hamlib_ipc_decode(h, packet, (size_t)got)) return -1;
   return 0;
}
static int ptt_release_verified(RIG *rig) {
   if (rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_OFF) != RIG_OK) return -1;
   ptt_t state = RIG_PTT_ON;
   if (rig_get_ptt(rig, RIG_VFO_CURR, &state) != RIG_OK || state != RIG_PTT_OFF)
      return -1;
   return 0;
}
static int parse_int(const char *value, int low, int high, int *out) {
   if (!value || !*value) return -1;
   char *end = NULL;
   errno = 0;
   long number = strtol(value, &end, 10);
   if (errno || !end || *end || number < low || number > high) return -1;
   *out = (int)number;
   return 0;
}
static int run(int fd, pid_t parent, int model, const char *device, int baud, int timeout) {
   if (prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent) return 3;
   struct sigaction sa = { .sa_handler = stop_signal };
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGTERM, &sa, NULL) || sigaction(SIGINT, &sa, NULL)) return 3;
   /* Refuse unrelated callers or wrong transport; no public socket listener. */
   int kind = 0;
   socklen_t length = sizeof(kind);
   if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &kind, &length) ||
       length != sizeof(kind) || kind != SOCK_SEQPACKET) return 3;
#ifdef SO_PEERCRED
   struct ucred peer;
   length = sizeof(peer);
   if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) ||
       length != sizeof(peer) || peer.pid != parent || peer.uid != getuid())
      return 3;
#endif
   int flags = fcntl(fd, F_GETFL);
   if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK)) return 3;
   RIG *rig = rig_init(model);
   if (!rig) return 4;
   char speed[32], timeout_string[32];
   snprintf(speed, sizeof(speed), "%d", baud);
   snprintf(timeout_string, sizeof(timeout_string), "%d", timeout);
   rig_set_conf(rig, rig_token_lookup(rig, "rig_pathname"), device);
   rig_set_conf(rig, rig_token_lookup(rig, "serial_speed"), speed);
   rig_set_conf(rig, rig_token_lookup(rig, "timeout"), timeout_string);
   rig_set_conf(rig, rig_token_lookup(rig, "retry"), "0");
   if (rig_open(rig) != RIG_OK) { rig_cleanup(rig); return 4; }

   /* The worker never assumes a transmitter is unkeyed after a failure.
    * Do not announce readiness without independent CAT confirmation.
    */
   if (ptt_release_verified(rig)) {
      fprintf(stderr, "rrhamlib: startup PTT OFF not confirmed; refusing service\n");
      rig_close(rig); rig_cleanup(rig); return 5;
   }
   uint8_t packet[MAX_PACKET];
   rr_hamlib_ipc_header_t h;
   if (send_packet(fd, RR_HAMLIB_IPC_HELLO, 0, NULL, 0)) goto disconnected;
   bool accepted = false;
   int64_t last_heartbeat = monotonic_ms();
   int status = 0;
   while (!quitting && getppid() == parent) {
      int64_t now = monotonic_ms();
      if (now < 0 || now - last_heartbeat > 1500) { status = 6; break; }
      struct pollfd p = { .fd = fd, .events = POLLIN };
      int ready = poll(&p, 1, 100);
      if (ready < 0 && errno == EINTR) continue;
      if (ready <= 0) { if (ready < 0) { status = 6; break; } continue; }
      if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { status = 6; break; }
      if (!(p.revents & POLLIN) || receive_packet(fd, &h, packet, sizeof(packet))) { status = 6; break; }
      const uint8_t *payload = packet + RR_HAMLIB_IPC_HEADER_SIZE;
      if (!accepted) {
         if (h.type != RR_HAMLIB_IPC_HELLO_ACK || h.payload_size != 0) { status = 6; break; }
         accepted = true;
         last_heartbeat = now;
         continue;
      }
      if (h.type == RR_HAMLIB_IPC_HEARTBEAT && h.payload_size == 0) {
         last_heartbeat = now;
         continue;
      }
      if (h.type == RR_HAMLIB_IPC_RELEASE && h.payload_size == 0) {
         uint8_t result = ptt_release_verified(rig) ? 1 : 0;
         if (send_packet(fd, RR_HAMLIB_IPC_RELEASE_RESULT, h.sequence, &result, 1)) { status = 6; break; }
         continue;
      }
      /* No key-down, control writes or leases accepted in this stage. */
      if (h.type == RR_HAMLIB_IPC_REQUEST || h.type == RR_HAMLIB_IPC_LEASE_RENEW) {
         uint8_t failure = 1;
         if (send_packet(fd, RR_HAMLIB_IPC_REPLY, h.sequence, &failure, 1)) { status = 6; break; }
         continue;
      }
      (void)payload;
      status = 6;
      break;
   }
   if (ptt_release_verified(rig)) {
      fprintf(stderr, "rrhamlib: shutdown PTT OFF not confirmed; radio state UNKNOWN\n");
      status = 7;
   }
   rig_close(rig);
   rig_cleanup(rig);
   return status;
disconnected:
   if (ptt_release_verified(rig)) fprintf(stderr, "rrhamlib: disconnect PTT release unconfirmed\n");
   rig_close(rig);
   rig_cleanup(rig);
   return 6;
}
int main(int argc, char **argv) {
   int model = 0, baud = 38400, timeout = 250, parent = 0;
   const char *device = NULL;
   /* Private command line. Parent supplies socketpair on fd 3. */
   if (argc != 11 || strcmp(argv[1], "--parent") ||
       parse_int(argv[2], 1, INT_MAX, &parent) ||
       strcmp(argv[3], "--model") || parse_int(argv[4], 1, INT_MAX, &model) ||
       strcmp(argv[5], "--device") || !(device = argv[6]) || !*device ||
       strcmp(argv[7], "--baud") || parse_int(argv[8], 1, 4000000, &baud) ||\n       strcmp(argv[9], "--timeout-ms") || parse_int(argv[10], 1, 5000, &timeout)) {
      fprintf(stderr, "usage: rrhamlib --parent PID --model ID --device PATH --baud RATE --timeout-ms MS\n");
      return 2;
   }
   return run(WORKER_FD, (pid_t)parent, model, device, baud, timeout);
}
