// SPDX-License-Identifier: GPL-2.0-only
/* Real rrhamlib against a disposable Hamlib NET rigctl model 2 endpoint.
 * The endpoint is rigctld -m 1 (Hamlib dummy), started by the Python test.
 * Never authorizes or sends TX key-down.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../rrserver/hamlib.ipc.h"
#include "../../rrserver/hamlib.transport.h"

static void command(rr_hamlib_transport_t *t, uint8_t op, uint8_t vfo,
   uint32_t value, uint8_t result[RR_HAMLIB_IPC_RESULT_SIZE]) {
   uint8_t payload[RR_HAMLIB_IPC_COMMAND_SIZE] = { 0 };
   payload[0] = op;
   payload[1] = vfo;
   rr_hamlib_ipc_put32(payload + 4, value);
   uint32_t size = 0;
   if (!rr_hamlib_transport_exchange(t, RR_HAMLIB_IPC_REQUEST,
         payload, sizeof(payload), RR_HAMLIB_IPC_REPLY,
         result, &size, RR_HAMLIB_IPC_RESULT_SIZE, 5000)) {
      fprintf(stderr, "FAIL: IPC transport failed for operation %u\n", op);
      exit(1);
   }
   if (size != RR_HAMLIB_IPC_RESULT_SIZE || result[0]) {
      fprintf(stderr, "FAIL: Hamlib rejected operation %u: length=%u status=%u\n",
         op, size, result[0]);
      exit(1);
   }
}
static void release(rr_hamlib_transport_t *t) {
   uint8_t status = 255;
   uint32_t length = 0;
   assert(rr_hamlib_transport_exchange(t, RR_HAMLIB_IPC_RELEASE, NULL, 0,
      RR_HAMLIB_IPC_RELEASE_RESULT, &status, &length, 1, 5000));
   assert(length == 1 && status == 0);
}
static void verify_poll(rr_hamlib_transport_t *t, uint32_t desired_freq,
   uint32_t desired_mode) {
   uint8_t r[RR_HAMLIB_IPC_RESULT_SIZE];
   command(t, RR_HAMLIB_OP_POLL, 0, 0, r);
   uint32_t frequency = rr_hamlib_ipc_get32(r + 4);
   uint32_t mode = rr_hamlib_ipc_get32(r + 8);
   fprintf(stderr, "dummy poll: freq=%" PRIu32 " mode=%" PRIu32 " ptt=%u\n",
      frequency, mode, r[1]);
   assert(r[1] == 0);
   assert(frequency == desired_freq);
   assert(mode == desired_mode);
}
int main(int argc, char **argv) {
   if (argc != 4) {
      fprintf(stderr, "usage: %s /absolute/path/rrhamlib host:port [normal|kill|restart|drop|lease]\n", argv[0]);
      return 2;
   }
   const char *worker = argv[1], *device = argv[2], *scenario = argv[3];
   rr_hamlib_transport_t t;
   if (!rr_hamlib_transport_open(&t, worker, 2, device, 38400, 250)) {
      fprintf(stderr, "FAIL: cannot open real rrhamlib against dummy rigctld\n");
      return 1;
   }
   assert(rr_hamlib_transport_ready(&t));
   fprintf(stderr, "rrhamlib pid=%ld connected to dummy rigctld\n", (long)t.pid);

   uint8_t result[RR_HAMLIB_IPC_RESULT_SIZE];
   command(&t, RR_HAMLIB_OP_FREQ_SET, 0, 14074000, result);
   command(&t, RR_HAMLIB_OP_MODE_SET, 0, RR_HAMLIB_MODE_USB, result);
   verify_poll(&t, 14074000, RR_HAMLIB_MODE_USB);
   release(&t);

   if (!strcmp(scenario, "kill")) {
      pid_t child = t.pid;
      assert(child > 0 && kill(child, SIGKILL) == 0);
      /* EOF must invalidate every subsequent control request. It must
       * never treat a dead worker as a successful PTT release.
       */
      uint8_t payload[RR_HAMLIB_IPC_COMMAND_SIZE] = { 0 };
      payload[0] = RR_HAMLIB_OP_POLL;
      uint32_t length = 0;
      assert(!rr_hamlib_transport_exchange(&t, RR_HAMLIB_IPC_REQUEST,
         payload, sizeof(payload), RR_HAMLIB_IPC_REPLY,
         result, &length, sizeof(result), 3000));
      assert(!rr_hamlib_transport_ready(&t));
      rr_hamlib_transport_close(&t);
      puts("PASS: killed real rrhamlib causes fail-closed IPC");
      return 0;
   }
   if (!strcmp(scenario, "lease")) {
      /* A real dummy rig is keyed only after an explicit short lease.
       * No parent heartbeat may prolong transmission. Expiry must dekey
       * even while the client stays connected and idle.
       */
      uint8_t approved = 1;
      uint32_t size = 0;
      assert(rr_hamlib_transport_exchange(&t, RR_HAMLIB_IPC_LEASE_RENEW,
         NULL, 0, RR_HAMLIB_IPC_REPLY, &approved, &size, 1, 2000));
      assert(size == 1 && approved == 0);
      command(&t, RR_HAMLIB_OP_PTT_ON, 0, 0, result);
      command(&t, RR_HAMLIB_OP_POLL, 0, 0, result);
      assert(result[1] == 1);
      /* Heartbeat sender continues every 250ms, but TX lease is 1500ms. */
      usleep(2200000);
      command(&t, RR_HAMLIB_OP_POLL, 0, 0, result);
      assert(result[1] == 0);
      approved = 0;
      size = 0;
      assert(rr_hamlib_transport_exchange(&t, RR_HAMLIB_IPC_LEASE_RENEW,
         NULL, 0, RR_HAMLIB_IPC_REPLY, &approved, &size, 1, 2000));
      assert(size == 1 && approved != 0);
      release(&t);
      rr_hamlib_transport_close(&t);
      puts("PASS: dummy PTT ON expires and is released despite live IPC heartbeat");
      return 0;
   }
   if (!strcmp(scenario, "restart")) {
      pid_t first_pid = t.pid;
      assert(first_pid > 0 && kill(first_pid, SIGKILL) == 0);
      uint8_t payload[RR_HAMLIB_IPC_COMMAND_SIZE] = { 0 };
      payload[0] = RR_HAMLIB_OP_POLL;
      uint32_t size = 0;
      assert(!rr_hamlib_transport_exchange(&t, RR_HAMLIB_IPC_REQUEST,
         payload, sizeof(payload), RR_HAMLIB_IPC_REPLY,
         result, &size, sizeof(result), 3000));
      assert(!rr_hamlib_transport_ready(&t));
      rr_hamlib_transport_close(&t);
      /* Explicit recovery creates a clean process/session and requires a
       * new startup PTT OFF confirmation. Not automatic supervisor restart.
       */
      assert(rr_hamlib_transport_open(&t, worker, 2, device, 38400, 250));
      assert(rr_hamlib_transport_ready(&t));
      verify_poll(&t, 14074000, RR_HAMLIB_MODE_USB);
      command(&t, RR_HAMLIB_OP_FREQ_SET, 0, 7074000, result);
      verify_poll(&t, 7074000, RR_HAMLIB_MODE_USB);
      release(&t);
      rr_hamlib_transport_close(&t);
      puts("PASS: explicit worker recovery re-handshakes and restores CAT control");
      return 0;
   }
   if (!strcmp(scenario, "drop")) {
      /* Parent Python process terminates rigctld only after this marker. */
      puts("READY_FOR_DROP");
      fflush(stdout);
      if (getchar() == EOF) {
         rr_hamlib_transport_close(&t);
         return 3;
      }
      uint8_t payload[RR_HAMLIB_IPC_COMMAND_SIZE] = { 0 };
      payload[0] = RR_HAMLIB_OP_POLL;
      uint32_t size = 0;
      bool ok = rr_hamlib_transport_exchange(&t, RR_HAMLIB_IPC_REQUEST,
         payload, sizeof(payload), RR_HAMLIB_IPC_REPLY,
         result, &size, sizeof(result), 5000);
      /* CAT can return a protocol-level error while IPC itself remains
       * connected. Either is acceptable; a valid poll is not.
       */
      assert(!ok || size != sizeof(result) || result[0] != 0);
      /* The child cannot truthfully confirm a PTT release after CAT loss. */
      uint8_t ptt_result = 255;
      size = 0;
      ok = rr_hamlib_transport_exchange(&t, RR_HAMLIB_IPC_RELEASE,
         NULL, 0, RR_HAMLIB_IPC_RELEASE_RESULT,
         &ptt_result, &size, 1, 5000);
      assert(!ok || size != 1 || ptt_result != 0);
      rr_hamlib_transport_close(&t);
      puts("PASS: CAT endpoint disappearance never yields confirmed poll/PTT release");
      return 0;
   }
   if (strcmp(scenario, "normal")) {
      fprintf(stderr, "unknown scenario: %s\n", scenario);
      rr_hamlib_transport_close(&t);
      return 2;
   }
   /* Heartbeat continues while backend sits idle. */
   usleep(700000);
   verify_poll(&t, 14074000, RR_HAMLIB_MODE_USB);
   rr_hamlib_transport_close(&t);
   puts("PASS: real rrhamlib with dummy rigctld: tune, poll, release, idle, shutdown");
   return 0;
}
