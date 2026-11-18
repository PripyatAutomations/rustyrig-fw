// SPDX-License-Identifier: GPL-2.0-only
/* RustyRig PSK worker: fd 4 RX PCM, fd 1 decoded text, fd 0 TX text commands ("TX message\n"), fd 5 modulated 16 kHz mono S16LE. This worker never requests
 * PTT: the client core gates its PCM on the manually connected PTT control and the server's active TX media. */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <psk.h>
#include "../common/pcm16k.h"

static void decoded(char c, void *data) {
   (void)data;
   fputc((unsigned char)c, stdout);
   fflush(stdout);
}
static void feed(const float *buf, size_t n, void *context) {
   psk_rx_feed((psk_rx_t *) context, buf, n);
}
static void *receive_thread(void *data) {
   rr_modem_rx_loop(feed, data);

   return NULL;
}
static int16_t pcm(float x) {
   x = fmaxf(-1.f, fminf(1.f, x * .75f));

   return (int16_t) lrintf(x * 32767.f);
}
int main(int argc, char **argv) {
   psk_mode_t mode = PSK_MODE_BPSK31;
   float carrier = 1500.f;

   if (argc > 1) {
      const char *names[] = {
         "BPSK31", "QPSK31", "BPSK63", "BPSK125", "QPSK63", "QPSK125"
      };
      int found = 0;

      for (int i = 0 ; i < 6 ; i++) {
         if (!strcmp(argv[1], names[i])) {
            mode = (psk_mode_t) i;
            found = 1;
            break;
         }
      }

      if (!found) {
         return 2;
      }
   }

   if (argc > 2) {
      char *end = NULL;
      carrier = strtof(argv[2], &end);

      if (!end || *end || carrier < 100.f || carrier > 3500.f) {
         return 2;
      }
   }
   psk_rx_t *rx = psk_rx_create(8000, carrier, mode);
   psk_tx_t *tx = psk_tx_create(8000, carrier, mode);

   if (!rx || !tx) {
      return 1;
   }
   setvbuf(stdout, NULL, _IONBF, 0);
   psk_rx_set_char_callback(rx, decoded, NULL);
   pthread_t receiver;

   if (pthread_create(&receiver, NULL, receive_thread, rx)) {
      return 1;
   }
   pthread_detach(receiver);

   char command[512];
   size_t used = 0;
   int16_t output[320];
   float samples[160];
   size_t pending = 0, sent = 0;
   float previous = 0.f;
   int transmitting = 0;

   for ( ;; ) {
      struct pollfd pfd = {
         .fd = 0, .events = POLLIN
      };
      int rc = poll(&pfd, 1, 20);

      if (rc < 0) {
         if (errno == EINTR) {
            continue;
         }
         break;
      }

      if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
         break;
      }

      if (rc > 0 && (pfd.revents & POLLIN)) {
         char buf[256];
         ssize_t n = read(0, buf, sizeof(buf));

         if (n <= 0) {
            if (n < 0 && errno == EINTR) {
               continue;
            }
            break;
         }

         for (ssize_t i = 0 ; i < n ; i++) {
            if (buf[i] == '\n') {
               command[used] = 0;

               if (!strcmp(command, "CANCEL")) {
                  psk_tx_destroy(tx);
                  tx = psk_tx_create(8000, carrier, mode);
                  pending = sent = 0;
                  transmitting = 0;
                  previous = 0.f;

                  if (!tx) {
                     break;
                  }
               } else if (used > 3 && !strncmp(command, "TX ", 3)) {
                  if (psk_tx_write(tx, command + 3, used - 3) > 0) {
                     transmitting = 1;
                  }
               }
               used = 0;
            } else if (buf[i] != '\r') {
               if (used < sizeof(command) - 1) {
                  command[used++] = buf[i];
               } else {
                  used = 0;     /* Reject oversized command. */
               }
            }
         }
      }

      if (pending) {
         ssize_t n = send(5, (char *)output + sent, pending - sent, MSG_DONTWAIT | MSG_NOSIGNAL);

         if (n > 0) {
            sent += (size_t)n;
         } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            break;
         }

         if (sent == pending) {
            pending = sent = 0;
         }
      }

      if (!pending && !psk_tx_idle(tx)) {
         if (psk_tx_read(tx, samples, 160) != 160) {
            break;
         }

         for (size_t i = 0 ; i < 160 ; i++) {
            output[i * 2] = pcm((previous + samples[i]) * .5f);
            output[i * 2 + 1] = pcm(samples[i]);
            previous = samples[i];
         }

         pending = sizeof(output);
         sent = 0;
      }

      if (transmitting && !pending && psk_tx_idle(tx)) {
         /* A SEQPACKET control completion cannot interleave with RX text. */
         static const char done[] = "TX_DONE";
         send(3, done, sizeof(done) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
         transmitting = 0;
      }
   }

   /* Child exit reaps the RX thread; never race its state by freeing rx. */
   psk_tx_destroy(tx);

   return 0;
}
