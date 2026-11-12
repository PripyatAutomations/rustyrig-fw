// SPDX-License-Identifier: GPL-2.0-only
/* One RX-only local modem worker consuming a chosen client PCM stream.
 * External executables only. Never forward FD5 to any transmitter.
 */
#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <libfwdspmgr/dsp-worker.h>
#include <rrclient/audio.h>
#include <rrclient/local.modem.h>
#include <rrclient/ui.h>

static GMutex modem_lock;
static rr_dsp_worker_t modem_worker = {
   .pid = -1, .serial_fd = -1, .control_fd = -1,
   .rx_pcm_fd = -1, .tx_pcm_fd = -1
};
static char modem_source[64], modem_engine[24];
static guint modem_poll_id;
static char decoded[512];
static unsigned decoded_used;
static guint64 dropped;
static gint64 last_report_us;

static void modem_flush_text(void) {
   if (!decoded_used) return;
   decoded[decoded_used] = 0;
   ui_print(NULL, "[local %s / %s] %s", modem_engine, modem_source, decoded);
   decoded_used = 0;
}
static void modem_stop(void);
static gboolean modem_poll(gpointer unused) {
   (void)unused;
   char bytes[256];
   int fd;
   g_mutex_lock(&modem_lock);
   fd = modem_worker.serial_fd;
   g_mutex_unlock(&modem_lock);
   if (fd < 0) return G_SOURCE_CONTINUE;
   for (unsigned n = 0; n < 8; n++) {
      ssize_t got = read(fd, bytes, sizeof(bytes));
      if (got < 0 && errno == EINTR) continue;
      if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
      if (got == 0 || (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
         ui_print(NULL, "Local modem worker closed its output");
         modem_poll_id = 0;
         modem_stop();
         return G_SOURCE_REMOVE;
      }
      for (ssize_t i = 0; i < got; i++) {
         unsigned char c = (unsigned char)bytes[i];
         if (c == '\r' || c == '\n') {
            modem_flush_text();
         } else if (c >= 32 || c == '\t') {
            if (decoded_used == sizeof(decoded) - 1) modem_flush_text();
            decoded[decoded_used++] = (char)c;
         }
      }
   }
   gint64 now = g_get_monotonic_time();
   if (decoded_used && now - last_report_us > 300000) {
      modem_flush_text();
      last_report_us = now;
   }
   return G_SOURCE_CONTINUE;
}
void rr_local_modem_feed(const char *source, const int16_t *pcm, size_t frames) {
   if (!source || !pcm || !frames || !g_mutex_trylock(&modem_lock)) return;
   if (modem_worker.rx_pcm_fd >= 0 && !strcmp(source, modem_source)) {
      size_t bytes = frames * sizeof(*pcm);
      ssize_t n = send(modem_worker.rx_pcm_fd, pcm, bytes, MSG_DONTWAIT | MSG_NOSIGNAL);
      if (n != (ssize_t)bytes) dropped++;
   }
   g_mutex_unlock(&modem_lock);
}
static void modem_stop(void) {
   if (modem_poll_id) {
      g_source_remove(modem_poll_id);
      modem_poll_id = 0;
   }
   modem_flush_text();
   g_mutex_lock(&modem_lock);
   rr_dsp_worker_stop(&modem_worker);
   modem_source[0] = modem_engine[0] = 0;
   g_mutex_unlock(&modem_lock);
}
void rr_local_modem_shutdown(void) {
   modem_stop();
}
static bool select_source(const char *source) {
   struct rrclient_rx_stream list[128] = {{0}};
   size_t n = audio_rx_streams(list, G_N_ELEMENTS(list));
   for (size_t i = 0; i < n && i < G_N_ELEMENTS(list); i++)
      if (!strcmp(list[i].id, source)) return true;
   return false;
}
static const char *modem_path(const char *engine) {
   const char *exe = !strcmp(engine, "psk") ? "rrmodem-psk" :
                     !strcmp(engine, "ft8") ? "rrmodem-ft8" : "rrmodem-olivia";
   static char candidate[256];
   snprintf(candidate, sizeof(candidate), "/var/lib/rustyrig/modems/%s", exe);
   if (access(candidate, X_OK) == 0) return candidate;
   snprintf(candidate, sizeof(candidate), "modems/%s", exe);
   if (access(candidate, X_OK) == 0) return candidate;
   if (!strcmp(engine, "ft8")) {
      snprintf(candidate, sizeof(candidate), "modems/ft8/%s", exe);
      if (access(candidate, X_OK) == 0) return candidate;
   }
   return NULL;
}
bool cmd_local_modem(int argc, char **argv) {
   if (argc == 2 && !strcasecmp(argv[1], "list")) {
      struct rrclient_rx_stream streams[128] = {{0}};
      size_t count = audio_rx_streams(streams, G_N_ELEMENTS(streams));
      for (size_t i = 0; i < count && i < G_N_ELEMENTS(streams); i++)
         ui_print(NULL, "%s  (%s / %s)", streams[i].id, streams[i].server, streams[i].room);
      return false;
   }
   if (argc == 2 && !strcasecmp(argv[1], "status")) {
      ui_print(NULL, modem_worker.pid > 0 ? "Local %s on %s (dropped PCM writes=%llu)" :
               "No local modem running", modem_engine, modem_source, (unsigned long long)dropped);
      return false;
   }
   if (argc == 2 && !strcasecmp(argv[1], "stop")) {
      modem_stop();
      ui_print(NULL, "Local modem stopped");
      return false;
   }
   if (argc >= 4 && argc <= 6 && !strcasecmp(argv[1], "start") &&
       (!strcasecmp(argv[2], "psk") || !strcasecmp(argv[2], "olivia") ||
        !strcasecmp(argv[2], "ft8"))) {
      const bool psk = !strcasecmp(argv[2], "psk");
      const bool ft8 = !strcasecmp(argv[2], "ft8");
      if (ft8 && argc != 4) return true;
      if (!select_source(argv[3])) {
         ui_print(NULL, "Unknown PCM stream '%s'; use /modem list", argv[3]);
         return true;
      }
      const char *path = modem_path(psk ? "psk" : ft8 ? "ft8" : "olivia");
      if (!path) {
         ui_print(NULL, "Modem executable unavailable; run make -C modems install");
         return true;
      }
      char command[512];
      const char *first = argc >= 5 ? argv[4] : (psk ? "BPSK31" : "8");
      const char *second = argc >= 6 ? argv[5] : (psk ? "1500" : "500");
      /* Only allow numeric options or well-known PSK enum names.
       * Prevent argument injection into spawned executable.
       */
      bool safe1 = true, safe2 = true;
      for (const char *p = first; *p; p++)
         if (!g_ascii_isalnum(*p)) safe1 = false;
      for (const char *p = second; *p; p++)
         if (!g_ascii_isdigit(*p)) safe2 = false;
      if (!safe1 || !safe2 || strlen(first) > 24 || strlen(second) > 8) return true;
      if (ft8) snprintf(command, sizeof(command), "%s", path);
      else snprintf(command, sizeof(command), "%s %s %s", path, first, second);
      modem_stop();
      g_mutex_lock(&modem_lock);
      rr_dsp_worker_init(&modem_worker);
      bool ok = rr_dsp_worker_spawn(&modem_worker, command);
      if (ok) {
         snprintf(modem_source, sizeof(modem_source), "%s", argv[3]);
         snprintf(modem_engine, sizeof(modem_engine), "%s", psk ? "PSK" : ft8 ? "FT8" : "Olivia");
         dropped = 0;
      }
      g_mutex_unlock(&modem_lock);
      if (!ok) {
         ui_print(NULL, "Unable to spawn %s", command);
         return true;
      }
      modem_poll_id = g_timeout_add(100, modem_poll, NULL);
      ui_print(NULL, "Local RX-only %s listening to %s; RF TX disabled",
               modem_engine, modem_source);
      return false;
   }
   ui_print(NULL, "Usage: /modem list | status | stop | start <psk|olivia|ft8> <PCM-stream> [mode|tones] [carrier-Hz|bandwidth-Hz]");
   return true;
}
