// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "hamlib.transport.h"
#include "hamlib.ipc.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
enum { HEADER = RR_HAMLIB_IPC_HEADER_SIZE, PACKET = HEADER + RR_HAMLIB_IPC_MAX_PAYLOAD };
static int64_t now_ms(void) {
   struct timespec ts;
   if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
   return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static bool send_frame(rr_hamlib_transport_t *t, uint16_t type, uint32_t seq,
   const void *payload, uint32_t length) {
   if (length > RR_HAMLIB_IPC_MAX_PAYLOAD || (length && !payload)) return false;
   uint8_t packet[PACKET];
   const rr_hamlib_ipc_header_t h = {
      .magic = RR_HAMLIB_IPC_MAGIC, .version = RR_HAMLIB_IPC_VERSION,
      .type = type, .sequence = seq, .payload_size = length
   };
   if (!rr_hamlib_ipc_encode(packet, sizeof(packet), &h)) return false;
   if (length) memcpy(packet + HEADER, payload, length);
   pthread_mutex_lock(&t->send_mutex);
   ssize_t sent = send(t->fd, packet, HEADER + length, MSG_NOSIGNAL | MSG_DONTWAIT);
   pthread_mutex_unlock(&t->send_mutex);
   return sent == (ssize_t)(HEADER + length);
}
static bool recv_frame(rr_hamlib_transport_t *t, rr_hamlib_ipc_header_t *h,
   uint8_t *buffer, int timeout_ms) {
   if (timeout_ms < 0 || timeout_ms > 30000) return false;
   struct pollfd pfd = { .fd = t->fd, .events = POLLIN };
   const int64_t until = now_ms() + timeout_ms;
   for (;;) {
      int64_t remaining = until - now_ms();
      if (remaining < 0) return false;
      int rc = poll(&pfd, 1, (int)remaining);
      if (rc < 0 && errno == EINTR) continue;
      if (rc <= 0 || (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))) return false;
      struct iovec iov = { .iov_base = buffer, .iov_len = PACKET };
      struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
      ssize_t got = recvmsg(t->fd, &msg, MSG_DONTWAIT);
      if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
      if (got <= 0 || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) return false;
      return rr_hamlib_ipc_decode(h, buffer, (size_t)got);
   }
}
static void *heartbeat_main(void *user) {
   rr_hamlib_transport_t *t = user;
   while (atomic_load(&t->running) && atomic_load(&t->connected)) {
      struct timespec pause = { .tv_sec = 0, .tv_nsec = 250000000L };
      nanosleep(&pause, NULL);
      if (!atomic_load(&t->running)) break;
      if (!send_frame(t, RR_HAMLIB_IPC_HEARTBEAT, 0, NULL, 0)) {
         atomic_store(&t->connected, false);
         break;
      }
   }
   return NULL;
}
static void bounded_reap(pid_t pid) {
   if (pid <= 0) return;
   int status;
   for (int i = 0; i < 15; ++i) {
      pid_t got = waitpid(pid, &status, WNOHANG);
      if (got == pid || (got < 0 && errno == ECHILD)) return;
      struct timespec delay = { .tv_sec = 0, .tv_nsec = 100000000L };
      nanosleep(&delay, NULL);
   }
   kill(pid, SIGTERM);
   for (int i = 0; i < 10; ++i) {
      pid_t got = waitpid(pid, &status, WNOHANG);
      if (got == pid || (got < 0 && errno == ECHILD)) return;
      struct timespec delay = { .tv_sec = 0, .tv_nsec = 100000000L };
      nanosleep(&delay, NULL);
   }
   /* The child may be stuck inside a Hamlib call. SIGKILL cannot release
    * CAT PTT: this must be surfaced as unsafe/unknown by the caller.
    */
   fprintf(stderr, "rrhamlib: worker did not exit; SIGKILL cannot guarantee RF release\n");
   kill(pid, SIGKILL);
   while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
}
bool rr_hamlib_transport_open(rr_hamlib_transport_t *t, const char *path,
   int model, const char *device, int baud, int timeout_ms) {
   if (!t || !path || path[0] != '/' || !device || !*device || model < 1 || baud < 1 || timeout_ms < 1 || timeout_ms > 5000)
      return false;
   memset(t, 0, sizeof(*t));
   t->fd = -1;
   if (pthread_mutex_init(&t->send_mutex, NULL)) return false;
   atomic_init(&t->running, false);
   atomic_init(&t->connected, false);
   int sockets[2] = { -1, -1 };
   posix_spawn_file_actions_t actions;
   if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets)) goto fail;
   if (posix_spawn_file_actions_init(&actions)) goto fail;
   /* Close the parent's end first: it can itself occupy fd 3. */
   int error = posix_spawn_file_actions_addclose(&actions, sockets[0]);
   if (!error && sockets[1] != 3)
      error = posix_spawn_file_actions_adddup2(&actions, sockets[1], 3);
   if (!error && sockets[1] != 3)
      error = posix_spawn_file_actions_addclose(&actions, sockets[1]);
   /* The child must not inherit server TLS sockets, databases, or radio FDs.
    * Actions run in order: first dup the private socket to fd 3, then close
    * every other inherited descriptor. Never silently drop this isolation.
    */
#if defined(__GLIBC__) && __GLIBC_PREREQ(2, 34)
   if (!error)
      error = posix_spawn_file_actions_addclosefrom_np(&actions, 4);
#else
   if (!error)
      error = ENOTSUP;
#endif
   if (error) {
      posix_spawn_file_actions_destroy(&actions);
      goto fail;
   }
   char parent_arg[32], model_arg[32], baud_arg[32], timeout_arg[32];
   snprintf(parent_arg, sizeof(parent_arg), "%ld", (long)getpid());
   snprintf(model_arg, sizeof(model_arg), "%d", model);
   snprintf(baud_arg, sizeof(baud_arg), "%d", baud);
   snprintf(timeout_arg, sizeof(timeout_arg), "%d", timeout_ms);
   char *const argv[] = {
      (char *)path, "--parent", parent_arg, "--model", model_arg,
      "--device", (char *)device, "--baud", baud_arg,
      "--timeout-ms", timeout_arg, NULL
   };
   int spawned = posix_spawn(&t->pid, path, &actions, NULL, argv, environ);
   posix_spawn_file_actions_destroy(&actions);
   close(sockets[1]);
   sockets[1] = -1;
   if (spawned) goto fail;
   t->fd = sockets[0];
   sockets[0] = -1;
   uint8_t packet[PACKET];
   rr_hamlib_ipc_header_t hello;
   if (!recv_frame(t, &hello, packet, 7000) ||
       hello.type != RR_HAMLIB_IPC_HELLO || hello.sequence != 0 ||
       hello.payload_size != 0 ||
       !send_frame(t, RR_HAMLIB_IPC_HELLO_ACK, 0, NULL, 0)) goto fail;
   atomic_store(&t->running, true);
   atomic_store(&t->connected, true);
   if (pthread_create(&t->heartbeat, NULL, heartbeat_main, t)) goto fail;
   t->heartbeat_started = true;
   return true;
fail:
   if (sockets[0] >= 0) close(sockets[0]);
   if (sockets[1] >= 0) close(sockets[1]);
   rr_hamlib_transport_close(t);
   return false;
}
bool rr_hamlib_transport_ready(const rr_hamlib_transport_t *t) {
   return t && atomic_load(&t->running) && atomic_load(&t->connected);
}
bool rr_hamlib_transport_exchange(rr_hamlib_transport_t *t, uint16_t type,
   const void *payload, uint32_t length, uint16_t reply_type,
   void *reply, uint32_t *reply_length, uint32_t capacity,
   int timeout_ms) {
   if (!rr_hamlib_transport_ready(t) || !reply_length || capacity > RR_HAMLIB_IPC_MAX_PAYLOAD ||
       (capacity && !reply)) return false;
   /* Server-side backend.worker serializes these requests per rig. */
   if (++t->sequence == 0) ++t->sequence;
   uint32_t seq = t->sequence;
   if (!send_frame(t, type, seq, payload, length)) {
      atomic_store(&t->connected, false);
      return false;
   }
   uint8_t packet[PACKET];
   rr_hamlib_ipc_header_t h;
   if (!recv_frame(t, &h, packet, timeout_ms) ||
       h.type != reply_type || h.sequence != seq ||
       h.payload_size > capacity) {
      atomic_store(&t->connected, false);
      return false;
   }
   if (h.payload_size) memcpy(reply, packet + HEADER, h.payload_size);
   *reply_length = h.payload_size;
   return true;
}
void rr_hamlib_transport_close(rr_hamlib_transport_t *t) {
   if (!t) return;
   atomic_store(&t->running, false);
   if (t->heartbeat_started) {
      pthread_join(t->heartbeat, NULL);
      t->heartbeat_started = false;
   }
   if (t->fd >= 0) {
      close(t->fd);
      t->fd = -1;
   }
   if (t->pid > 0) {
      bounded_reap(t->pid);
      t->pid = 0;
   }
   atomic_store(&t->connected, false);
   pthread_mutex_destroy(&t->send_mutex);
}
