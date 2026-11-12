// SPDX-License-Identifier: GPL-2.0-only
/* GTK-free, receive-only external decoder supervisor.
 * Owns up to eight worker processes on the client core context. PCM taps
 * use trylock/nonblocking sockets; never block audio or forward TX PCM/PTT.
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

typedef struct {
   rr_dsp_worker_t worker;
   bool active;
   char source[64], engine[16], option[24];
   unsigned value;
   guint64 drops, received;
   char text[RR_LOCAL_MODEM_TEXT];
   size_t text_len;
} decoder_t;
static decoder_t decoders[RR_LOCAL_MODEM_MAX];
static GMutex decoder_lock;
static guint poll_id;
static bool initialized;
static char last_status[192];

static void append_text(decoder_t *d, const char *bytes, size_t n) {
   for (size_t i = 0; i < n; i++) {
      unsigned char ch = (unsigned char)bytes[i];
      if (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') continue;
      if (d->text_len + 2 >= sizeof(d->text)) {
         memmove(d->text, d->text + 512, d->text_len - 512);
         d->text_len -= 512;
      }
      d->text[d->text_len++] = ch == '\r' ? '\n' : (char)ch;
      d->text[d->text_len] = 0;
   }
}
static void stop_one(unsigned index) {
   decoder_t *d = &decoders[index];
   g_mutex_lock(&decoder_lock);
   if (d->active) {
      rr_dsp_worker_stop(&d->worker);
      d->active = false;
   }
   g_mutex_unlock(&decoder_lock);
}
static const char *worker_path(const char *engine) {
   const char *exe = !strcmp(engine, "psk") ? "rrmodem-psk" :
                     !strcmp(engine, "olivia") ? "rrmodem-olivia" : "rrmodem-ft8";
   static char candidate[256];
   snprintf(candidate, sizeof(candidate), "/var/lib/rustyrig/modems/%s", exe);
   if (!access(candidate, X_OK)) return candidate;
   snprintf(candidate, sizeof(candidate), "modems/%s", exe);
   if (!access(candidate, X_OK)) return candidate;
   if (!strcmp(engine, "ft8")) {
      snprintf(candidate, sizeof(candidate), "modems/ft8/%s", exe);
      if (!access(candidate, X_OK)) return candidate;
   }
   return NULL;
}
static gboolean poll_decoders(gpointer unused) {
   (void)unused;
   for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++) {
      decoder_t *d = &decoders[i];
      if (!d->active) continue;
      bool ended = false;
      char bytes[256];
      for (unsigned k = 0; k < 8; k++) {
         ssize_t n = recv(d->worker.serial_fd, bytes, sizeof(bytes), MSG_DONTWAIT);
         if (n > 0) {
            g_mutex_lock(&decoder_lock);
            append_text(d, bytes, (size_t)n);
            d->received += (unsigned)n;
            g_mutex_unlock(&decoder_lock);
         } else if (n == 0) {
            ended = true;
            break;
         } else if (errno == EINTR) {
            continue;
         } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) ended = true;
            break;
         }
      }
      if (ended) {
         g_snprintf(last_status, sizeof(last_status), "Decoder %u exited", i + 1);
         stop_one(i);
      }
   }
   return G_SOURCE_CONTINUE;
}
void rr_local_modem_feed(const char *source, const int16_t *pcm, size_t frames) {
   if (!source || !pcm || !frames || frames > 2048 ||
       !g_mutex_trylock(&decoder_lock)) return;
   for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++) {
      decoder_t *d = &decoders[i];
      if (!d->active || d->worker.rx_pcm_fd < 0 ||
          strcmp(d->source, source)) continue;
      size_t bytes = frames * sizeof(*pcm);
      ssize_t n = send(d->worker.rx_pcm_fd, pcm, bytes,
                       MSG_DONTWAIT | MSG_NOSIGNAL);
      if (n != (ssize_t)bytes) d->drops++;
   }
   g_mutex_unlock(&decoder_lock);
}
static bool source_present(const char *source) {
   struct rrclient_rx_stream streams[RR_LOCAL_MODEM_STREAMS] = {{0}};
   size_t n = audio_rx_streams(streams, G_N_ELEMENTS(streams));
   for (size_t i = 0; i < n && i < G_N_ELEMENTS(streams); i++)
      if (!strcmp(streams[i].id, source)) return true;
   return false;
}
static bool start_one(const rr_local_modem_request_t *req) {
   if (!memchr(req->source, 0, sizeof(req->source)) ||
       !memchr(req->engine, 0, sizeof(req->engine)) ||
       !memchr(req->option, 0, sizeof(req->option)) ||
       !source_present(req->source)) {
      g_strlcpy(last_status, "Invalid or unavailable PCM source", sizeof(last_status));
      return false;
   }
   const char *engine = req->engine;
   if (strcmp(engine, "psk") && strcmp(engine, "olivia") && strcmp(engine, "ft8")) {
      g_strlcpy(last_status, "Unknown decoder engine", sizeof(last_status));
      return false;
   }
   unsigned index = RR_LOCAL_MODEM_MAX;
   if (req->slot > 0 && req->slot <= RR_LOCAL_MODEM_MAX)
      index = req->slot - 1;
   else if (req->slot == 0)
      for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++)
         if (!decoders[i].active) { index = i; break; }
   if (index == RR_LOCAL_MODEM_MAX) {
      g_strlcpy(last_status, "All eight decoder slots are in use", sizeof(last_status));
      return false;
   }
   const char *exe = worker_path(engine);
   if (!exe) {
      g_strlcpy(last_status, "Worker executable missing; build/install the modems", sizeof(last_status));
      return false;
   }
   char command[512];
   if (!strcmp(engine, "psk")) {
      static const char *const modes[] = {
         "BPSK31", "QPSK31", "BPSK63", "BPSK125", "QPSK63", "QPSK125"
      };
      bool recognized = false;
      for (unsigned i = 0; i < G_N_ELEMENTS(modes); i++)
         if (!strcmp(req->option, modes[i])) recognized = true;
      if (!recognized || req->value < 100 || req->value > 3500) {
         g_strlcpy(last_status, "Invalid PSK mode or center frequency", sizeof(last_status));
         return false;
      }
      g_snprintf(command, sizeof(command), "%s %s %u", exe, req->option, req->value);
   } else if (!strcmp(engine, "olivia")) {
      unsigned tones = (unsigned)g_ascii_strtoull(req->option, NULL, 10);
      if ((tones != 4 && tones != 8 && tones != 16 && tones != 32 && tones != 64) ||
          (req->value != 125 && req->value != 250 && req->value != 500 &&
           req->value != 1000 && req->value != 2000)) {
         g_strlcpy(last_status, "Invalid Olivia tones or bandwidth", sizeof(last_status));
         return false;
      }
      g_snprintf(command, sizeof(command), "%s %u %u", exe, tones, req->value);
   } else {
      g_snprintf(command, sizeof(command), "%s", exe);
   }
   stop_one(index);
   decoder_t *d = &decoders[index];
   rr_dsp_worker_init(&d->worker);
   if (!rr_dsp_worker_spawn(&d->worker, command)) {
      g_snprintf(last_status, sizeof(last_status), "Failed to launch %s", command);
      return false;
   }
   g_mutex_lock(&decoder_lock);
   d->active = true;
   g_strlcpy(d->source, req->source, sizeof(d->source));
   g_strlcpy(d->engine, engine, sizeof(d->engine));
   g_strlcpy(d->option, req->option, sizeof(d->option));
   d->value = req->value;
   d->text_len = 0;
   d->text[0] = 0;
   d->drops = d->received = 0;
   g_mutex_unlock(&decoder_lock);
   g_snprintf(last_status, sizeof(last_status),
              "Decoder %u started: %s / %s (RX-only)", index+1, engine, req->source);
   return true;
}
static void publish_snapshot(void) {
   rr_local_modem_snapshot_t *snapshot = g_new0(rr_local_modem_snapshot_t, 1);
   struct rrclient_rx_stream streams[RR_LOCAL_MODEM_STREAMS] = {{0}};
   size_t n = audio_rx_streams(streams, G_N_ELEMENTS(streams));
   snapshot->sources = MIN(n, G_N_ELEMENTS(streams));
   for (unsigned i = 0; i < snapshot->sources; i++)
      g_strlcpy(snapshot->source_id[i], streams[i].id,
                sizeof(snapshot->source_id[i]));
   g_mutex_lock(&decoder_lock);
   snapshot->count = RR_LOCAL_MODEM_MAX;
   for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++) {
      const decoder_t *d = &decoders[i];
      rr_local_modem_entry_t *entry = &snapshot->entries[i];
      entry->slot = i+1;
      entry->running = d->active;
      entry->value = d->value;
      entry->drops = d->drops;
      entry->received = d->received;
      g_strlcpy(entry->source, d->source, sizeof(entry->source));
      g_strlcpy(entry->engine, d->engine, sizeof(entry->engine));
      g_strlcpy(entry->option, d->option, sizeof(entry->option));
      g_strlcpy(entry->text, d->text, sizeof(entry->text));
   }
   g_mutex_unlock(&decoder_lock);
   g_strlcpy(snapshot->status, last_status, sizeof(snapshot->status));
   rr_event_message_t response = {
      .name = RR_LOCAL_MODEM_RESULT,
      .payload = snapshot, .length = sizeof(*snapshot)
   };
   event_emit_message(&response);
   g_free(snapshot);
}
static void request_received(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!message || !message->payload ||
       message->length != sizeof(rr_local_modem_request_t)) return;
   const rr_local_modem_request_t *req = message->payload;
   if (req->action == RR_LOCAL_MODEM_START) start_one(req);
   else if (req->action == RR_LOCAL_MODEM_STOP &&
            req->slot > 0 && req->slot <= RR_LOCAL_MODEM_MAX) {
      stop_one(req->slot - 1);
      g_snprintf(last_status, sizeof(last_status), "Decoder %u stopped", req->slot);
   }
   publish_snapshot();
}
void rr_local_modem_init(void) {
   if (initialized) return;
   initialized = true;
   for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++)
      rr_dsp_worker_init(&decoders[i].worker);
   event_on_message_domain_token(RR_LOCAL_MODEM_REQUEST, request_received, NULL, "core");
   poll_id = g_timeout_add(100, poll_decoders, NULL);
}
void rr_local_modem_request(const rr_local_modem_request_t *req) {
   if (!req) return;
   rr_event_message_t message = {
      .name = RR_LOCAL_MODEM_REQUEST, .payload = req, .length = sizeof(*req)
   };
   event_emit_message(&message);
}
void rr_local_modem_shutdown(void) {
   if (!initialized) return;
   if (poll_id) g_source_remove(poll_id);
   poll_id = 0;
   for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++) stop_one(i);
   initialized = false;
}
bool cmd_local_modem(int argc, char **argv) {
   if (argc == 2 && !strcasecmp(argv[1], "list")) {
      struct rrclient_rx_stream streams[RR_LOCAL_MODEM_STREAMS] = {{0}};
      size_t n = audio_rx_streams(streams, G_N_ELEMENTS(streams));
      for (size_t i = 0; i < n && i < G_N_ELEMENTS(streams); i++)
         ui_print(NULL, "%s (%s / %s)", streams[i].id,
                  streams[i].server, streams[i].room);
      return false;
   }
   if (argc == 2 && !strcasecmp(argv[1], "status")) {
      for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++) {
         decoder_t *d = &decoders[i];
         if (d->active)
            ui_print(NULL, "#%u %s %s %s %u (drops=%llu)", i+1,
                     d->source, d->engine, d->option, d->value,
                     (unsigned long long)d->drops);
      }
      return false;
   }
   if (argc >= 3 && !strcasecmp(argv[1], "stop")) {
      unsigned id = (unsigned)atoi(argv[2]);
      if (id < 1 || id > RR_LOCAL_MODEM_MAX) return true;
      stop_one(id-1);
      return false;
   }
   if (argc >= 4 && argc <= 7 && !strcasecmp(argv[1], "start")) {
      rr_local_modem_request_t req = {.action=RR_LOCAL_MODEM_START};
      req.slot = argc >= 7 ? (unsigned)atoi(argv[6]) : 0;
      g_strlcpy(req.engine, argv[2], sizeof(req.engine));
      g_strlcpy(req.source, argv[3], sizeof(req.source));
      const bool psk = !strcmp(req.engine, "psk");
      const bool ft8 = !strcmp(req.engine, "ft8");
      g_strlcpy(req.option, argc >= 5 ? argv[4] : (psk ? "BPSK31" : ft8 ? "" : "8"),
                sizeof(req.option));
      req.value = argc >= 6 ? (unsigned)atoi(argv[5]) : (psk ? 1500u : ft8 ? 0u : 500u);
      return !start_one(&req);
   }
   ui_print(NULL, "Usage: /modem list | status | stop <slot> | start <psk|olivia|ft8> <stream> [mode|tones] [carrier|bandwidth] [slot]");
   return true;
}
