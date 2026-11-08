//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
// Kiwi-specific networking and wire protocol; no client/UI state belongs here.
#include <librrprotocol/bridge.sdr.h>
#include <rrbridge/spectrum.row.h>
#include <librustyaxe/core.h>
#include <librustyaxe/json.h>
#include <librustyaxe/module.h>
#include <librustyaxe/transport.wss.h>
#include <librustyaxe/transport.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

#define KIWI_QUEUE_MAX 16384
struct kiwi_instance {
   rrp_sdr_host_t host;
   rr_transport_t *connection;
   rr_transport_t *waterfall;
   bool waterfall_enabled, waterfall_ready, waterfall_first_frame_logged, waterfall_setup_logged;
   uint64_t waterfall_rows, waterfall_retry, waterfall_last_sent, waterfall_keepalive;
   uint64_t wf_stats_at, wf_received, wf_throttled, wf_ipc_dropped, wf_invalid, wf_sent;
   uint64_t traffic_rx, traffic_tx, traffic_report_at;
   uint64_t snd_last_rx, snd_last_tx, wf_last_rx, wf_last_tx;
   unsigned waterfall_zoom;
   int agc, hang, thresh, slope, decay, man_gain, squelch, squelch_max;
   int nb, nb_gate, nb_thresh, nb_click, de_emp;
   uint64_t waterfall_center_hz;
   bool started, authenticated, configured, online, closing, confirmed, failed;
   unsigned rate, failures, attempts, redirects;
   bool redirecting;
   uint64_t max_hz, hz, wanted_hz, request, deadline, retry, keepalive, received, online_since;
   char mode[16], wanted_mode[16], url[512], redirect_url[512];
   uint32_t sequence;
   bool have_sequence;
};
/* Native socket, DNS, TLS and WebSocket state belong to RustyAxe. */
static void waterfall_center(struct kiwi_instance *k);
static void kiwi_traffic(struct kiwi_instance *k, rr_transport_t *transport, bool waterfall);
static bool kiwi_noise_blanker(struct kiwi_instance *k);
static const char *setting(struct kiwi_instance *k, const char *key) {
   return k->host.setting ? k->host.setting(k->host.user, key) : NULL;
}
static void note(struct kiwi_instance *k, const char *text) {
   if (k->host.status) {
      k->host.status(k->host.user, text);
   }
}
static void unavailable(struct kiwi_instance *k) {
   if (k->waterfall) {
      rr_transport_close(k->waterfall);
   }
   uint64_t request = k->request;
   k->request = 0;

   // Complete first: the server cancels any remaining request on offline state.
   if (request && k->host.complete) {
      k->host.complete(k->host.user, request, false);
   }

   if (k->host.state) {
      k->host.state(k->host.user, false, 0, NULL);
   }
   k->online = false;

   if (k->confirmed) {
      k->wanted_hz = k->hz;
      snprintf(k->wanted_mode, sizeof(k->wanted_mode), "%s", k->mode);
   }
}
static void fail(struct kiwi_instance *k, const char *reason) {
   if (k->failed) {
      return;
   }
   k->failed = true;
   note(k, reason);
   unavailable(k);

   if (k->connection) {
      rr_transport_close(k->connection);
   }
}
static bool send_command(struct kiwi_instance *k, const char *command) {
   if (!k->connection || rr_transport_state(k->connection) != RR_TRANSPORT_OPEN || rr_transport_queued_bytes(k->connection) > KIWI_QUEUE_MAX) {
      return false;
   }

   return rr_transport_send_message(k->connection, command, strlen(command)) == RR_TRANSPORT_OK;
}
static void *kiwi_create(const rrp_sdr_host_t *host) {
   struct kiwi_instance *k = host ? calloc(1, sizeof(*k)) : NULL;

   if (!k) {
      return NULL;
   }
   k->host = *host;
   k->max_hz = 30000000;
   k->waterfall_zoom = 6;
   k->agc = 1;
   k->hang = 0;
   k->thresh = -100;
   k->slope = 6;
   k->decay = 1000;
   k->man_gain = 50;
   k->nb_gate = 100;
   k->nb_thresh = 50;
   k->hz = k->wanted_hz = 14074000;
   strcpy(k->mode, "USB");
   strcpy(k->wanted_mode, "USB");

   return k;
}
static void kiwi_destroy(void *instance) {
   struct kiwi_instance *k = instance;
   k->closing = true;

   if (k->connection) {
      rr_transport_close(k->connection);
      rr_transport_poll(k->connection, 0);
      kiwi_traffic(k, k->connection, false);

      if (rr_transport_state(k->connection) == RR_TRANSPORT_CLOSED) {
         rr_transport_wss_release(k->connection);
      }
   }

   if (k->waterfall) {
      rr_transport_close(k->waterfall);
      rr_transport_poll(k->waterfall, 0);
      kiwi_traffic(k, k->waterfall, true);

      if (rr_transport_state(k->waterfall) == RR_TRANSPORT_CLOSED) {
         rr_transport_wss_release(k->waterfall);
      }
   }
   free(k);
}
static bool kiwi_capabilities(void *instance, rrp_sdr_capabilities_t *out) {
   if (!instance || !out) {
      return false;
   }
   *out = (rrp_sdr_capabilities_t) {
      .rx = true, .rx_audio = true, .rx_waterfall = true, .waterfall_zoom = true, .waterfall_zoom_min = 0, .waterfall_zoom_max = 14, .rx_min_hz = 0, .rx_max_hz
         = ((struct kiwi_instance *)instance)->max_hz, .rx_modes = "USB LSB AM CW"
   };

   return true;
}
static bool kiwi_tune(void *instance, uint64_t hz, const char *mode, char *command, size_t capacity) {
   const char *kiwi_mode = NULL;
   int n;

   if (!instance || !mode || !command || !capacity || hz > ((struct kiwi_instance *)instance)->max_hz) {
      return false;
   }

   if (!strcasecmp(mode, "usb")) {
      kiwi_mode = "usb";
   } else if (!strcasecmp(mode, "lsb")) {
      kiwi_mode = "lsb";
   } else if (!strcasecmp(mode, "am")) {
      kiwi_mode = "am";
   } else if (!strcasecmp(mode, "cw")) {
      kiwi_mode = "cw";
   } else {
      return false;
   }
   // Kiwi client.py set_mod/_default_passbands uses kHz and Hz cutoffs:
   // https://github.com/jks-prv/kiwiclient/blob/master/kiwi/client.py
   int low = 300, high = 2700;

   if (!strcmp(kiwi_mode, "lsb")) {
      low = -2700;
      high = -300;
   } else if (!strcmp(kiwi_mode, "am")) {
      low = -4900;
      high = 4900;
   } else if (!strcmp(kiwi_mode, "cw")) {
      low = 300;
      high = 700;
   }
   n = snprintf(command, capacity, "SET mod=%s low_cut=%d high_cut=%d freq=%" PRIu64 ".%03" PRIu64, kiwi_mode, low, high, hz / 1000, hz % 1000);

   return n > 0 && (size_t)n < capacity;
}


static bool configure(struct kiwi_instance *k) {
   if (!k->authenticated || !k->rate || k->configured) {
      return true;
   }
   char tune[160], agc[160], squelch[64];
   snprintf(agc, sizeof(agc), "SET agc=%d hang=%d thresh=%d slope=%d decay=%d manGain=%d", k->agc, k->hang, k->thresh, k->slope, k->decay, k->man_gain);
   snprintf(squelch, sizeof(squelch), "SET squelch=%d max=%d", k->squelch, k->squelch_max);

   char de_emp[32];
   snprintf(de_emp, sizeof(de_emp), "SET de_emp=%d", k->de_emp);

   if (!kiwi_tune(k, k->wanted_hz, k->wanted_mode, tune, sizeof(tune)) ||
      !send_command(k, "SET compression=0") || !send_command(k, squelch) ||
      !send_command(k, "SET genattn=0") || !send_command(k, "SET gen=0 mix=-1") ||
      !send_command(k, agc) || !send_command(k, de_emp) || !kiwi_noise_blanker(k) || !send_command(k, tune)) {
      return false;
   }
   k->configured = true;
   k->deadline = mono_us() + 5000000;

   return true;
}
static int kiwi_url_decode(const char *src, size_t n, char *dst, size_t capacity) {
   if (n + 1 > capacity) {
      return -1;
   }
   size_t used = 0;

   for (size_t i = 0 ; i < n ; i++) {
      unsigned char value = (unsigned char)src[i];

      if (value == '%') {
         if (i + 2 >= n || !isxdigit((unsigned char)src[i + 1]) || !isxdigit((unsigned char)src[i + 2])) {
            return -1;
         }
         char hex[3] = {
            src[i + 1], src[i + 2], 0
         };
         value = (unsigned char)strtoul(hex, NULL, 16);
         i += 2;
      }

      if (!value) {
         return -1;
      }
      dst[used++] = (char)value;
   }

   dst[used] = 0;

   return (int)used;
}
static void receiver_metadata(struct kiwi_instance *k, const char *encoded) {
   if (strlen(encoded) > 60000) {
      return;
   }
   char *text = malloc(strlen(encoded) + 1);

   if (!text) {
      return;
   }
   int length = kiwi_url_decode(encoded, strlen(encoded), text, strlen(encoded) + 1);

   if (length >= 0) {
      text[length] = 0;
      dict *json = json2dict(text);

      if (json) {
         long long api = dict_get_llong(json, "ext_api", -1);

         if (api >= 0 && api <= 1000000) {
            char allowance[32];
            snprintf(allowance, sizeof(allowance), "%lld", api);

            if (k->host.metadata) {
               k->host.metadata(k->host.user, "ext_api", allowance);
            }

            if (!api) {
               fail(k, "Kiwi receiver has disabled external API access");
            }
         }
         const char *remote[] = {
            "rx_name", "rx_location", "admin_email", "rx_antenna", "rx_device", "rx_grid"
         };
         const char *local[] = {
            "name", "location", "contact", "antenna", "hardware", "grid"
         };

         for (unsigned i = 0 ; i < sizeof(remote) / sizeof(remote[0]) ; i++) {
            const char *value = dict_get(json, remote[i], NULL);

            if (value && *value && k->host.metadata) {
               k->host.metadata(k->host.user, local[i], value);
            }
         }

         dict_free(json);
      }
   }
   free(text);
}
static bool parameter(struct kiwi_instance *k, const char *key, const char *value) {
   if (!strcmp(key, "load_cfg")) {
      receiver_metadata(k, value);

      if (k->failed) {
         return false;
      }
   } else if (!strcmp(key, "badp")) {
      if (strcmp(value, "0")) {
         fail(k, "Kiwi authentication rejected");

         return false;
      }
      k->authenticated = true;
   } else if (!strcmp(key, "too_busy")) {
      fail(k, "Kiwi receiver rejected session: channel capacity or external-client limit (too_busy)");

      return false;
   } else if (!strcmp(key, "down")) {
      fail(k, "Kiwi receiver reports offline/maintenance");

      return false;
   } else if (!strcmp(key, "redirect")) {
      fail(k, "Kiwi receiver requested a redirect; automatic redirects are unsupported");

      return false;
   } else if (!strcmp(key, "password_timeout")) {
      fail(k, "Kiwi receiver password-entry timeout");

      return false;
   } else if (!strcmp(key, "bandwidth")) {
      char *end;
      unsigned long long bandwidth = strtoull(value, &end, 10);

      if (end == value || *end || !bandwidth || bandwidth > 1000000000) {
         return false;
      }
      // Kiwi advertises its tunable baseband in Hz. Limit it to the subset
      // implemented by this driver; nonzero frequency offsets remain unsupported.
      k->max_hz = bandwidth < 30000000 ? bandwidth : 30000000;
   } else if (!strcmp(key, "sample_rate")) {
      char *end;
      unsigned long rate = strtoul(value, &end, 10);

      if (end == value || (*end && *end != '.') || rate < 8000 || rate > 48000) {
         return false;
      }

      if (*end == '.') {
         for (const char *p = end + 1 ; *p ; p++) {
            if (!isdigit((unsigned char)*p)) {
               return false;
            }
         }
      }

      if (k->rate && k->rate != rate) {
         return false;
      }
      k->rate = rate;
   } else if (!strcmp(key, "audio_rate")) {
      char *end;
      unsigned long rate = strtoul(value, &end, 10);

      if (*end || rate < 8000 || rate > 48000) {
         return false;
      }
      char reply[80];
      snprintf(reply, sizeof(reply), "SET AR OK in=%lu out=44100", rate);

      if (!send_command(k, reply)) {
         return false;
      }
   } else if (!strcmp(key, "freq_offset")) {
      // Converted/offset receivers need a capability refresh before tuning.
      for (const char *p = value ; *p ; p++) {
         if (*p != '0' && *p != '.' && *p != '-') {
            fail(k, "Kiwi frequency offsets are not supported yet");

            return false;
         }
      }
   }

   return configure(k);
}
static bool kiwi_receive(void *instance, const void *frame, size_t length) {
   struct kiwi_instance *k = instance;
   const uint8_t *data = frame;

   if (!k || !data || length < 3 || length > 65536) {
      return false;
   }

   if (!memcmp(data, "MSG", 3)) {
      if (length < 4 || data[3] != ' ' || memchr(data + 4, 0, length - 4)) {
         return false;
      }
      char *text = malloc(length - 3);

      if (!text) {
         return false;
      }
      memcpy(text, data + 4, length - 4);
      text[length - 4] = 0;
      char *save = NULL;
      bool ok = true;

      for (char *token = strtok_r(text, " ", &save) ; token && ok ; token = strtok_r(NULL, " ", &save)) {
         char *value = strchr(token, '=');

         if (value) {
            *value++ = 0;
         }
         ok = parameter(k, token, value ? value : "");
      }

      free(text);

      return ok;
   }

   if (memcmp(data, "SND", 3)) {
      return true;              // Ignore non-audio telemetry.
   }

   if (length < 12 || !k->configured || !k->rate || (data[3] & 0x08)) {
      return false;
   }

   // Compression is explicitly disabled; tolerate only startup packets still
   // draining from Kiwi's previous ADPCM setting. Never label ADPCM as PCM.
   if (data[3] & 0x10) {
      return !k->online;
   }

   if ((length - 10) & 1) {
      return false;
   }
   uint32_t seq = (uint32_t) data[4] | ((uint32_t) data[5] << 8) | ((uint32_t) data[6] << 16) | ((uint32_t) data[7] << 24);

   if (k->have_sequence && (int32_t) (seq - k->sequence) <= 0) {
      return true;
   }
   k->have_sequence = true;
   k->sequence = seq;
   k->received = mono_us();

   // rx_sound.cpp sets NEW_FREQ (0x04) after applying frequency/mode.
   // Kiwi does not echo a numeric frequency/readback or RustyRig request ID.
   if ((!k->online || k->request) && (data[3] & 0x04)) {
      k->hz = k->wanted_hz;
      snprintf(k->mode, sizeof(k->mode), "%s", k->wanted_mode);

      if (k->waterfall && k->waterfall_ready) {
         waterfall_center(k);
      }

      if (!k->online) {
         k->online_since = mono_us();
         char text[96];
         snprintf(text, sizeof(text), "Kiwi receiver online (%u Hz mono PCM)", k->rate);
         note(k, text);
      }
      k->online = k->confirmed = true;

      if (k->host.state) {
         k->host.state(k->host.user, true, k->hz, k->mode);
      }

      if (k->request && k->host.complete) {
         k->host.complete(k->host.user, k->request, true);
      }
      k->request = 0;
   }

   // Tuning confirmation controls observed state, not audio delivery. Keep
   // feeding the existing stream while Kiwi applies a frequency/mode change.
   if (!k->online) {
      return true;
   }
   size_t count = (length - 10) / 2;

   for (size_t offset = 0 ; offset < count ; ) {
      int16_t pcm[2048];
      size_t take = count - offset;

      if (take > 2048) {
         take = 2048;
      }

      for (size_t i = 0 ; i < take ; i++) {
         const uint8_t *s = data + 10 + 2 * (offset + i);
         pcm[i] = (int16_t) ((data[3] & 0x80) ? ((uint16_t) s[1] << 8) | s[0] : ((uint16_t) s[0] << 8) | s[1]);
      }

      if (k->host.audio_pcm16) {
         k->host.audio_pcm16(k->host.user, pcm, take, k->rate);
      }
      offset += take;
   }

   return true;
}

/* W/F is an independent Kiwi websocket; never let spectrum failures interrupt SND or the audio watchdog. Uncompressed rows are 1024 bins. */
/* Kiwi waterfall start is a fixed-point low-edge offset, not kHz. A 1024-bin row at zoom 6 spans 1/64 of the full receiver bandwidth. MAX_ZOOM=14 gives a
 * 24-bit start scale for current KiwiSDR. */
static void waterfall_range(const struct kiwi_instance *k, uint64_t *low_hz, uint64_t *high_hz) {
   double full = (double)k->max_hz;
   double span = full / (1u << k->waterfall_zoom);
   double center = (double)(k->waterfall_center_hz ? k->waterfall_center_hz : (k->wanted_hz ? k->wanted_hz : k->hz));
   double low = center - span / 2.;

   if (low < 0.) {
      low = 0.;
   }

   if (low > full - span) {
      low = full - span;
   }
   *low_hz = (uint64_t) (low + .5);
   *high_hz = (uint64_t) (low + span + .5);
}
static void waterfall_center(struct kiwi_instance *k) {
   if (!k->waterfall || !k->max_hz) {
      return;
   }
   const unsigned zoom = k->waterfall_zoom;
   uint64_t low_hz, high_hz;
   waterfall_range(k, &low_hz, &high_hz);
   double full = (double)k->max_hz;
   double low = (double)low_hz;
   unsigned start = (unsigned)(low / full * (1024u << 14) + .5);
   char command[96];
   snprintf(command, sizeof(command), "SET zoom=%u start=%u", zoom, start);
   rr_transport_send_message(k->waterfall, command, strlen(command));
}

typedef struct kiwi_frame {
   struct {
      const uint8_t *buf;
      size_t len;
   } data;
   unsigned flags;
} kiwi_frame_t;
static void waterfall_ws(rr_transport_t *t, enum rr_transport_event event, rr_transport_channel_t *channel, const uint8_t *data, size_t len, void *user) {
   (void)channel;
   struct kiwi_instance *k = user;

   if (event == RR_TRANSPORT_EVENT_OPEN) {
      note(k, "Kiwi RF waterfall websocket opened");
      k->waterfall_keepalive = mono_us();
      const char *password = setting(k, "password");

      if (!password) {
         password = "";
      }

      if (strlen(password) > 128 || strpbrk(password, " \r\n")) {
         rr_transport_close(t);

         return;
      }
      char auth[192];
      snprintf(auth, sizeof(auth), "SET auth t=kiwi p=%s", password);
      rr_transport_send_message(t, auth, strlen(auth));
      /* Kiwi requires start= for waterfall positioning; cf= is not the corresponding W/F command. Initialize display range and pacing. */
      const char *commands[] = {
         "SERVER DE CLIENT RustyRig W/F",
         "SET wf_comp=0",
         "SET maxdb=-20 mindb=-120", "SET wf_speed=2", "SET interp=13"
      };

      for (size_t i = 0 ; i < sizeof(commands) / sizeof(commands[0]) ; i++) {
         rr_transport_send_message(t, commands[i], strlen(commands[i]));
      }

      waterfall_center(k);
   } else if (event == RR_TRANSPORT_EVENT_ERROR) {
      char detail[256];
      snprintf(detail, sizeof(detail), "Kiwi W/F connection error: %s", data ? (const char *)data : "unknown");
      note(k, detail);
   } else if (event == RR_TRANSPORT_EVENT_MESSAGE || event == RR_TRANSPORT_EVENT_BINARY_MESSAGE) {
      kiwi_frame_t frame = {
         .data = {
            .buf = data, .len = len
         }, .flags = (unsigned)event
      };
      kiwi_frame_t *m = &frame;

      if (!k->waterfall_first_frame_logged && m->data.len > 0) {
         k->waterfall_first_frame_logged = true;
         char detail[128];
         snprintf(detail, sizeof(detail), "Kiwi W/F frame: %zu bytes, type=0x%x", m->data.len, m->flags);
         note(k, detail);
      }

      if (m->data.len >= 3 && !memcmp(m->data.buf, "MSG", 3)) {
         /* Control messages precede the first spectrum row; these are not decoder failures and may carry receiver setup state. */
         if (!k->waterfall_setup_logged) {
            k->waterfall_setup_logged = true;
            char detail[220];
            size_t n = m->data.len < sizeof(detail) - 20 ? m->data.len : sizeof(detail) - 20;
            snprintf(detail, sizeof(detail), "Kiwi W/F setup: %.*s", (int)n, m->data.buf);
            note(k, detail);
         }
      }

      if (m->data.len >= 3 && !memcmp(m->data.buf, "W/F", 3)) {
         k->wf_received++;

         if (m->data.len != 1040) {
            k->wf_invalid++;
         }

         /* Default uncompressed Kiwi rows: fixed 16-byte header + 1024 bins. Keep parsing conservative pending normalized spectrum transport. */
         if (m->data.len != 1040 && !k->waterfall_ready) {
            char detail[128];
            snprintf(detail, sizeof(detail), "Kiwi RF waterfall: unexpected frame length %zu (expected 1040)", m->data.len);
            note(k, detail);
         }

         if (m->data.len == 1040) {
            /* Drop surplus W/F rows before IPC to protect the audio pipeline. Keep audio WebSocket and watchdog handling independent. */
            uint64_t now = mono_us();

            if (k->waterfall_last_sent && now - k->waterfall_last_sent < 100000) {
               k->wf_throttled++;

               return;
            }
            k->waterfall_last_sent = now;
            uint64_t low_hz, high_hz;
            waterfall_range(k, &low_hz, &high_hz);
            rr_bridge_spectrum_row row = {
               .magic = RR_BRIDGE_SPECTRUM_MAGIC, .version = 1,
               .low_hz = low_hz, .high_hz = high_hz,
               .sequence = (uint32_t) k->waterfall_rows,
               .bins = RR_BRIDGE_SPECTRUM_BINS
            };
            memcpy(row.power, m->data.buf + 16, RR_BRIDGE_SPECTRUM_BINS);
            int sent = rr_bridge_spectrum_send(RR_BRIDGE_SPECTRUM_FD, &row);

            if (sent == 1) {
               k->wf_sent++;
               k->waterfall_rows++;
            } else {
               k->wf_ipc_dropped++;
            }

            if (!k->waterfall_ready) {
               k->waterfall_ready = true;
               note(k, "Kiwi W/F stream ready (1024 RF power bins per row)");
            }
         }
      }
   } else if (event == RR_TRANSPORT_EVENT_CLOSE && t == k->waterfall) {
      if (k->waterfall_enabled) {
         note(k, k->waterfall_ready ? "Kiwi W/F socket closed; will retry independently of audio" : "Kiwi W/F socket closed before receiving spectrum data");
      }
      k->waterfall_ready = false;
      k->waterfall_first_frame_logged = false;
      k->waterfall_setup_logged = false;
      k->waterfall_retry = mono_us() + UINT64_C(10000000);
   }
}
static void websocket(rr_transport_t *t, enum rr_transport_event event, rr_transport_channel_t *channel, const uint8_t *data, size_t len, void *user) {
   (void)channel;
   struct kiwi_instance *k = user;

   if (event == RR_TRANSPORT_EVENT_OPEN) {
      const char *password = setting(k, "password");

      if (!password) {
         password = "";
      }

      if (strlen(password) > 128 || strpbrk(password, " \r\n")) {
         fail(k, "Invalid Kiwi password configuration");

         return;
      }
      char auth[192];
      snprintf(auth, sizeof(auth), "SET auth t=kiwi p=%s", password);

      if (!send_command(k, auth)) {
         fail(k, "Cannot send Kiwi authentication");
      }
   } else if ((event == RR_TRANSPORT_EVENT_MESSAGE || event == RR_TRANSPORT_EVENT_BINARY_MESSAGE) && !k->failed) {
      if (!kiwi_receive(k, data, len)) {
         fail(k, "Invalid or unsupported Kiwi stream");
      }
   } else if (event == RR_TRANSPORT_EVENT_ERROR && !k->closing) {
      char detail[200];
      snprintf(detail, sizeof(detail), "Kiwi WebSocket: %.*s", (int)(len < 150 ? len : 150), data ? (const char *)data : "unknown");
      fail(k, detail);
   } else if (event == RR_TRANSPORT_EVENT_CLOSE && t == k->connection) {
      if (!k->closing) {
         bool was_online = k->online;
         unavailable(k);

         if (was_online) {
            note(k, "Kiwi WebSocket connection lost after receiver was online");
         }

         if (k->attempts >= 3) {
            note(k, "Kiwi receiver stopped after 3 connection attempts");
            k->retry = UINT64_MAX;
         } else {
            unsigned delay = 1u << (k->failures < 6 ? k->failures : 6);

            if (k->failures < 6) {
               k->failures++;
            }
            char text[96];
            snprintf(text, sizeof(text), "Kiwi receiver disconnected; retrying in %u seconds", delay);
            note(k, text);
            k->retry = mono_us() + (uint64_t) delay * 1000000;
         }
      }
   }
}
static bool kiwi_start(void *instance) {
   struct kiwi_instance *k = instance;
   const char *host = setting(k, "host"), *port_text = setting(k, "port");

   if (!host || !*host || strlen(host) > 253) {
      return false;
   }

   for (const char *p = host ; *p ; p++) {
      if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-') {
         return false;
      }
   }

   char *end;
   unsigned long port = port_text ? strtoul(port_text, &end, 10) : 8073;

   if ((port_text && (*end || end == port_text)) || !port || port > 65535) {
      return false;
   }
   const char *frequency = setting(k, "frequency"), *mode = setting(k, "mode");

   if (frequency) {
      k->wanted_hz = strtoull(frequency, &end, 10);

      if (*end || end == frequency || k->wanted_hz > 30000000) {
         return false;
      }
   }

   if (mode) {
      snprintf(k->wanted_mode, sizeof(k->wanted_mode), "%s", mode);
   }

   for (char *p = k->wanted_mode ; *p ; p++) {
      *p = toupper((unsigned char)*p);
   }

   char tune[160];

   if (!kiwi_tune(k, k->wanted_hz, k->wanted_mode, tune, sizeof(tune))) {
      return false;
   }
   // A secure receiver uses the same Kiwi protocol over TLS.
   const char *secure = setting(k, "secure");
   bool tls = secure && (!strcasecmp(secure, "true") || !strcmp(secure, "1") || !strcasecmp(secure, "yes"));
   // Same timestamped SND route as kiwiclient, with a unique process suffix.
   snprintf(k->url, sizeof(k->url), "%s://%s:%lu/%" PRIu64 "/SND", tls ? "wss" : "ws", host, port, (uint64_t) time(NULL) * 1000 + (unsigned)getpid() % 1000);

   if (k->host.metadata) {
      char endpoint[320];
      snprintf(endpoint, sizeof(endpoint), "%s://%s:%lu/", tls ? "https" : "http", host, port);
      k->host.metadata(k->host.user, "url", endpoint);
   }
   const char *wf = setting(k, "waterfall");
   k->waterfall_enabled = wf && (!strcmp(wf, "1") || !strcasecmp(wf, "true") || !strcasecmp(wf, "yes"));
   k->started = true;

   return true;
}
static void kiwi_traffic(struct kiwi_instance *k, rr_transport_t *transport, bool waterfall) {
   uint64_t rx = 0, tx = 0;
   rr_transport_wss_traffic(transport, &rx, &tx);
   uint64_t *last_rx = waterfall ? &k->wf_last_rx : &k->snd_last_rx;
   uint64_t *last_tx = waterfall ? &k->wf_last_tx : &k->snd_last_tx;

   if (rx >= *last_rx) {
      k->traffic_rx += rx - *last_rx;
   }

   if (tx >= *last_tx) {
      k->traffic_tx += tx - *last_tx;
   }
   *last_rx = rx;
   *last_tx = tx;
}
static void kiwi_poll(void *instance) {
   struct kiwi_instance *k = instance;
   uint64_t current = mono_us();

   if (!k->started) {
      return;
   }

   if (!k->connection && current >= k->retry && k->attempts < 3) {
      k->attempts++;
      k->authenticated = k->configured = k->online = k->have_sequence = k->failed = false;
      k->rate = 0;
      /* Allow a per-bridge User-Agent; default to Chrome for Kiwi proxies. */
      const char *ua = setting(k, "user-agent");

      if (!ua || !*ua) {
         ua = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
      }
      /* Never allow configuration to inject additional HTTP headers. */
      size_t ua_len = strlen(ua);

      if (ua_len > 384 || strpbrk(ua, "\r\n") || ua_len == 0) {
         fail(k, "Invalid Kiwi User-Agent configuration");

         return;
      }
      rr_wss_client_options_t options = {
         .user_agent = ua, .allow_cross_origin_redirect = !(setting(k, "password") && *setting(k, "password"))
      };
      k->connection = rr_transport_wss_connect(k->url, &options, websocket, k);
      k->snd_last_rx = k->snd_last_tx = 0;
      k->deadline = current + 10000000;
      k->received = current;
      k->keepalive = current;

      if (!k->connection) {
         k->retry = current + 1000000;
      }
   }

   /* Waterfall is optional and independently retried. Never block audio. */
   if (k->waterfall_enabled && k->online && !k->waterfall && current >= k->waterfall_retry) {
      char wf_url[512];
      snprintf(wf_url, sizeof(wf_url), "%s", k->url);
      char *snd = strstr(wf_url, "/SND");

      if (snd && !snd[4]) {
         memcpy(snd + 1, "W/F", 4);
         const char *ua = setting(k, "user-agent");

         if (!ua || !*ua) {
            ua = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/131.0.0.0 Safari/537.36";
         }

         if (strlen(ua) < 384 && !strpbrk(ua, "\r\n")) {
            char detail[560];
            snprintf(detail, sizeof(detail), "Connecting Kiwi RF waterfall: %s", wf_url);
            note(k, detail);
            rr_wss_client_options_t options = {
               .user_agent = ua, .allow_cross_origin_redirect = false
            };
            k->waterfall = rr_transport_wss_connect(wf_url, &options, waterfall_ws, k);
            k->wf_last_rx = k->wf_last_tx = 0;
         }
      }
      k->waterfall_retry = current + UINT64_C(10000000);
   }

   if (k->connection) {
      rr_transport_poll(k->connection, 0);
      kiwi_traffic(k, k->connection, false);

      if (rr_transport_state(k->connection) == RR_TRANSPORT_CLOSED) {
         rr_transport_wss_release(k->connection);
         k->connection = NULL;
      }
   }

   if (k->waterfall) {
      rr_transport_poll(k->waterfall, 0);
      kiwi_traffic(k, k->waterfall, true);

      if (rr_transport_state(k->waterfall) == RR_TRANSPORT_CLOSED) {
         rr_transport_wss_release(k->waterfall);
         k->waterfall = NULL;
      }
   }
   current = mono_us();

   /* One cumulative IPC sample per second; the parent computes rates. This is not PCM/media IPC traffic and must not be counted twice. */
   if ((!k->traffic_report_at || current - k->traffic_report_at >= UINT64_C(1000000)) && k->host.metadata) {
      char total[32];
      snprintf(total, sizeof(total), "%" PRIu64, k->traffic_rx);
      k->host.metadata(k->host.user, "traffic.rx", total);
      snprintf(total, sizeof(total), "%" PRIu64, k->traffic_tx);
      k->host.metadata(k->host.user, "traffic.tx", total);
      k->traffic_report_at = current;
   }

   /* Separate intentional rate limiting from actual IPC loss. */
   if (!k->wf_stats_at) {
      k->wf_stats_at = current;
   }

   if (current - k->wf_stats_at >= UINT64_C(10000000)) {
      if (k->waterfall_enabled && k->online && !k->wf_received && !k->wf_sent) {
         char detail[256];
         snprintf(detail, sizeof(detail), "Kiwi RF waterfall/10s no frames: socket=%s ready=%s max_hz=%" PRIu64 " zoom=%u", !k->waterfall ? "disconnected" :
            rr_transport_state(k->waterfall) == RR_TRANSPORT_OPEN ? "open" : "connecting/closing", k->waterfall_ready ? "yes" : "no", k->max_hz, k->
            waterfall_zoom);
         note(k, detail);
      }

      if (k->wf_received || k->wf_throttled || k->wf_ipc_dropped || k->wf_invalid) {
         char detail[256];
         uint64_t dropped = k->wf_throttled + k->wf_ipc_dropped + k->wf_invalid;
         snprintf(detail, sizeof(detail), "Kiwi RF waterfall/10s received=%" PRIu64 " sent=%" PRIu64
            " throttle=%" PRIu64 " ipc-drop=%" PRIu64 " malformed=%" PRIu64
            " dropped=%.1f%%", k->wf_received, k->wf_sent, k->wf_throttled, k->wf_ipc_dropped, k->wf_invalid, k->wf_received ? 100.0 * (double)dropped / (
               double)k->wf_received : 0.0);
         note(k, detail);
      }
      k->wf_received = k->wf_sent = k->wf_throttled = 0;
      k->wf_ipc_dropped = k->wf_invalid = 0;
      k->wf_stats_at = current;
   }

   /* Kiwi W/F has its own session timer, separate from SND. */
   if (k->waterfall_enabled && k->waterfall && rr_transport_state(k->waterfall) == RR_TRANSPORT_OPEN && current - k->waterfall_keepalive >= 1000000) {
      const char *keepalive = "SET keepalive";

      if (rr_transport_queued_bytes(k->waterfall) < KIWI_QUEUE_MAX) {
         rr_transport_send_message(k->waterfall, keepalive, strlen(keepalive));
      }
      k->waterfall_keepalive = current;
   }

   if (!k->connection) {
      return;
   }

   if ((!k->online && current >= k->deadline) || (k->request && current >= k->deadline) || current - k->received > 10000000) {
      /* A media subscription only proves the RustyRig encoder is ready. Distinguish remote handshake, configuration, tune and stream stalls. */
      char reason[192];
      snprintf(reason, sizeof(reason), "Kiwi receiver timed out (%s; ws=%s auth=%s configured=%s online=%s rate=%u; last-audio=%" PRIu64 "ms)", k->request ?
         "tune acknowledgement" :
         !(rr_transport_state(k->connection) == RR_TRANSPORT_OPEN) ? "WebSocket handshake" :
         !k->authenticated ? "authentication" :
         !k->rate ? "sample-rate announcement" :
         !k->configured ? "receiver configuration" :
         !k->online ? "first tuned audio packet" : "audio stream stalled", (rr_transport_state(k->connection) == RR_TRANSPORT_OPEN) ? "yes" : "no", k->
         authenticated ? "yes" : "no", k->configured ? "yes" : "no", k->online ? "yes" : "no", k->rate, (uint64_t) ((current - k->received) / 1000));
      fail(k, reason);

      return;
   }

   if ((rr_transport_state(k->connection) == RR_TRANSPORT_OPEN) && current - k->keepalive >= 1000000) {
      if (!send_command(k, "SET keepalive")) {
         fail(k, "Cannot send Kiwi keepalive");
      }
      k->keepalive = current;
   }
}
static bool kiwi_set_receiver(void *instance, uint64_t request, uint64_t hz, const char *mode) {
   struct kiwi_instance *k = instance;

   if (!k->online || k->request || !request) {
      return false;
   }
   char tune[160];

   if (!kiwi_tune(k, hz, mode, tune, sizeof(tune))) {
      return false;
   }

   if (hz == k->hz && !strcmp(mode, k->mode)) {
      if (k->host.complete) {
         k->host.complete(k->host.user, request, true);
      }

      return true;
   }

   if (!send_command(k, tune)) {
      return false;
   }
   k->wanted_hz = hz;
   snprintf(k->wanted_mode, sizeof(k->wanted_mode), "%s", mode);

   if (k->waterfall) {
      waterfall_center(k);
   }
   k->request = request;
   k->deadline = mono_us() + 4000000;

   return true;
}

static bool kiwi_set_waterfall(void *instance, bool enabled, unsigned zoom, uint64_t center_hz) {
   struct kiwi_instance *k = instance;

   if (!k || zoom > 14 || center_hz > k->max_hz) {
      return false;
   }
   k->waterfall_zoom = zoom;
   k->waterfall_center_hz = center_hz;
   k->waterfall_enabled = enabled;
   k->waterfall_last_sent = 0;

   if (!enabled && k->waterfall) {
      rr_transport_close(k->waterfall);
      k->waterfall_retry = 0;
   } else if (enabled && k->waterfall && rr_transport_state(k->waterfall) == RR_TRANSPORT_OPEN) {
      waterfall_center(k);
   } else if (enabled) {
      k->waterfall_retry = 0;
   }

   return true;
}
static bool kiwi_noise_blanker(struct kiwi_instance *k) {
   char gate[80], threshold[80], enabled[48], click[48];
   snprintf(gate, sizeof(gate), "SET nb type=0 param=0 pval=%d", k->nb_gate);
   snprintf(threshold, sizeof(threshold), "SET nb type=0 param=1 pval=%d", k->nb_thresh);
   snprintf(enabled, sizeof(enabled), "SET nb type=0 en=%d", k->nb);
   snprintf(click, sizeof(click), "SET nb type=2 en=%d", k->nb_click);

   return send_command(k, "SET nb algo=1") && send_command(k, gate) &&
          send_command(k, threshold) && send_command(k, enabled) &&
          send_command(k, "SET nb type=2 param=0 pval=1") && send_command(k, "SET nb type=2 param=1 pval=1") && send_command(k, click);
}
static bool kiwi_control(void *instance, const char *name, const char *value) {
   struct kiwi_instance *k = instance;

   if (!k || !name || !value || !*value) {
      return false;
   }
   char *end = NULL;
   long v = strtol(value, &end, 10);

   if (end == value || *end) {
      return false;
   }
   int *target = NULL;
   int low = 0, high = 0;

   if (!strcmp(name, "agc")) {
      target = &k->agc;
      high = 1;
   } else if (!strcmp(name, "hang")) {
      target = &k->hang;
      high = 1;
   } else if (!strcmp(name, "threshold")) {
      target = &k->thresh;
      low = -130;
      high = 0;
   } else if (!strcmp(name, "slope")) {
      target = &k->slope;
      high = 20;
   } else if (!strcmp(name, "decay")) {
      target = &k->decay;
      low = 50;
      high = 5000;
   } else if (!strcmp(name, "manual-gain")) {
      target = &k->man_gain;
      high = 100;
   } else if (!strcmp(name, "squelch")) {
      target = &k->squelch;
      high = 1;
   } else if (!strcmp(name, "squelch-threshold")) {
      target = &k->squelch_max;
      high = 100;
   } else if (!strcmp(name, "noise-blanker")) {
      target = &k->nb;
      high = 1;
   } else if (!strcmp(name, "blanker-gate")) {
      target = &k->nb_gate;
      low = 100;
      high = 5000;
   } else if (!strcmp(name, "blanker-threshold")) {
      target = &k->nb_thresh;
      high = 100;
   } else if (!strcmp(name, "click-blanker")) {
      target = &k->nb_click;
      high = 1;
   } else if (!strcmp(name, "de-emphasis")) {
      target = &k->de_emp;
      high = 1;
   } else {
      return false;
   }

   if (v < low || v > high) {
      return false;
   }
   int previous = *target;
   *target = (int)v;
   char command[192];

   if (!strncmp(name, "squelch", 7)) {
      snprintf(command, sizeof(command), "SET squelch=%d max=%d", k->squelch, k->squelch_max);
   } else if (!strcmp(name, "de-emphasis")) {
      snprintf(command, sizeof(command), "SET de_emp=%d", k->de_emp);
   } else if (!strcmp(name, "noise-blanker") || !strcmp(name, "blanker-gate") || !strcmp(name, "blanker-threshold") || !strcmp(name, "click-blanker")) {
      if (kiwi_noise_blanker(k)) {
         return true;
      }
      *target = previous;

      return false;
   } else {
      snprintf(command, sizeof(command), "SET agc=%d hang=%d thresh=%d slope=%d decay=%d manGain=%d", k->agc, k->hang, k->thresh, k->slope, k->decay, k->
         man_gain);
   }

   if (send_command(k, command)) {
      return true;
   }
   *target = previous;

   return false;
}

static const rrp_sdr_driver_t kiwi_driver = {
   .abi_version = RRP_SDR_ABI_VERSION, .name = "kiwi", .create = kiwi_create,
   .destroy = kiwi_destroy, .capabilities = kiwi_capabilities, .start = kiwi_start, .poll = kiwi_poll,
   .set_receiver = kiwi_set_receiver, .tune = kiwi_tune, .receive = kiwi_receive,
   .set_waterfall = kiwi_set_waterfall, .set_control = kiwi_control
};

rr_module_event_t modexports[] = { {
                                      0
                                   } };
bool rr_module_init(void) {
   return rrp_sdr_register(&kiwi_driver);
}
void rr_module_shutdown(void) {
   rrp_sdr_unregister(&kiwi_driver);
}
