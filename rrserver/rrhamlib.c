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
static vfo_t physical_vfo(uint8_t index) {
   return index == 0 ? RIG_VFO_A : index == 1 ? RIG_VFO_B : RIG_VFO_NONE;
}
static rmode_t physical_mode(uint32_t mode) {
   switch (mode) {
      case RR_HAMLIB_MODE_CW: return RIG_MODE_CW;
      case RR_HAMLIB_MODE_AM: return RIG_MODE_AM;
      case RR_HAMLIB_MODE_LSB: return RIG_MODE_LSB;
      case RR_HAMLIB_MODE_USB: return RIG_MODE_USB;
      case RR_HAMLIB_MODE_FM: return RIG_MODE_FM;
      case RR_HAMLIB_MODE_PKTUSB: return RIG_MODE_PKTUSB;
      case RR_HAMLIB_MODE_PKTLSB: return RIG_MODE_PKTLSB;
      default: return RIG_MODE_NONE;
   }
}
static uint32_t wire_mode(rmode_t mode) {
   if (mode == RIG_MODE_CW) return RR_HAMLIB_MODE_CW;
   if (mode == RIG_MODE_AM) return RR_HAMLIB_MODE_AM;
   if (mode == RIG_MODE_LSB) return RR_HAMLIB_MODE_LSB;
   if (mode == RIG_MODE_USB) return RR_HAMLIB_MODE_USB;
   if (mode == RIG_MODE_FM) return RR_HAMLIB_MODE_FM;
   if (mode == RIG_MODE_PKTUSB) return RR_HAMLIB_MODE_PKTUSB;
   if (mode == RIG_MODE_PKTLSB) return RR_HAMLIB_MODE_PKTLSB;
   return RR_HAMLIB_MODE_NONE;
}
/* All Hamlib operations execute in this child. A nonzero status means the
 * server must not infer any success or confirmed RX/TX hardware state. */
static void execute_command(RIG *rig, const uint8_t *command, uint8_t *reply) {
   memset(reply, 0, RR_HAMLIB_IPC_RESULT_SIZE);
   reply[0] = 1;
   uint8_t operation = command[0], index = command[1];
   vfo_t vfo = physical_vfo(index);
   uint32_t requested = rr_hamlib_ipc_get32(command + 4);
   if (vfo == RIG_VFO_NONE || command[2] || command[3] ||
       rr_hamlib_ipc_get32(command + 8) != 0) return;
   int rc = -RIG_EINVAL;
   switch (operation) {
      case RR_HAMLIB_OP_POLL: {
         freq_t freq = 0;
         rmode_t mode = RIG_MODE_NONE;
         pbwidth_t width = 0;
         rc = rig_get_freq(rig, vfo, &freq);
         if (rc != RIG_OK || freq < 0 || freq > UINT32_MAX) return;
         rc = rig_get_mode(rig, vfo, &mode, &width);
         if (rc != RIG_OK) return;
         ptt_t ptt = RIG_PTT_ON;
         rc = rig_get_ptt(rig, vfo, &ptt);
         if (rc != RIG_OK) return;
         rr_hamlib_ipc_put32(reply + 4, (uint32_t)freq);
         rr_hamlib_ipc_put32(reply + 8, wire_mode(mode));
         rr_hamlib_ipc_put32(reply + 12, width > 0 && (uint64_t)width <= UINT32_MAX ?
            (uint32_t)width : 0);
         reply[1] = ptt == RIG_PTT_OFF ? 0 : 1;
         break;
      }
      case RR_HAMLIB_OP_FREQ_SET:
         rc = rig_set_freq(rig, vfo, (freq_t)requested);
         break;
      case RR_HAMLIB_OP_MODE_SET: {
         rmode_t mode = physical_mode(requested);
         if (mode == RIG_MODE_NONE) return;
         rc = rig_set_mode(rig, vfo, mode, RIG_PASSBAND_NOCHANGE);
         break;
      }
      case RR_HAMLIB_OP_WIDTH_SET: {
         rmode_t mode = RIG_MODE_NONE;
         pbwidth_t old_width = 0;
         if (!requested || rig_get_mode(rig, vfo, &mode, &old_width) != RIG_OK) return;
         rc = rig_set_mode(rig, vfo, mode, requested);
         break;
      }
      case RR_HAMLIB_OP_SWAP:
         rc = rig_vfo_op(rig, RIG_VFO_A, RIG_OP_XCHG);
         break;
      case RR_HAMLIB_OP_SPLIT_SET:
         if (requested > 1) return;
         rc = rig_set_split_vfo(rig, vfo, requested ? RIG_SPLIT_ON : RIG_SPLIT_OFF,
            index == 0 ? RIG_VFO_B : RIG_VFO_A);
         break;
      /* POWER_SET requires capability calibration and cannot safely use a
       * generic uncalibrated value. Refuse until that mapping is migrated. */
      default:
         return;
   }
   reply[0] = rc == RIG_OK ? 0 : 1;
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
      /* Key-down remains impossible. Only enumerated rig operations are
       * accepted, and every request gets an explicit result.
       */
      if (h.type == RR_HAMLIB_IPC_REQUEST) {
         if (h.payload_size != RR_HAMLIB_IPC_COMMAND_SIZE) { status = 6; break; }
         uint8_t result[RR_HAMLIB_IPC_RESULT_SIZE];
         execute_command(rig, payload, result);
         if (send_packet(fd, RR_HAMLIB_IPC_REPLY, h.sequence,
                         result, sizeof(result))) { status = 6; break; }
         continue;
      }
      if (h.type == RR_HAMLIB_IPC_LEASE_RENEW) {
         /* Lease support must be implemented before accepting any TX_ON. */
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
       strcmp(argv[7], "--baud") || parse_int(argv[8], 1, 4000000, &baud) ||
       strcmp(argv[9], "--timeout-ms") || parse_int(argv[10], 1, 5000, &timeout)) {
      fprintf(stderr, "usage: rrhamlib --parent PID --model ID --device PATH --baud RATE --timeout-ms MS\n");
      return 2;
   }
   return run(WORKER_FD, (pid_t)parent, model, device, baud, timeout);
}
