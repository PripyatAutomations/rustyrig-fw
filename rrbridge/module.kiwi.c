//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Kiwi-specific networking and wire protocol; no client/UI state belongs here.
#include <librrprotocol/bridge.sdr.h>
#include <librustyaxe/core.h>
#include <librustyaxe/module.h>
#include <ext/libmongoose/mongoose.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#define KIWI_QUEUE_MAX 16384
struct kiwi_instance {
   rrp_sdr_host_t host;
   struct mg_mgr manager;
   struct mg_connection *connection;
   bool started, authenticated, configured, online, closing, confirmed, failed;
   unsigned rate, failures, attempts, redirects;
   bool redirecting;
   uint64_t max_hz, hz, wanted_hz, request, deadline, retry, keepalive, received, online_since;
   char mode[16], wanted_mode[16], url[512], redirect_url[512];
   uint32_t sequence;
   bool have_sequence;
};
static const char *setting(struct kiwi_instance *k, const char *key) {
   return k->host.setting ? k->host.setting(k->host.user, key) : NULL;
}
static void note(struct kiwi_instance *k, const char *text) {
   if (k->host.status) {
      k->host.status(k->host.user, text);
   }
}
static void unavailable(struct kiwi_instance *k) {
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
      k->connection->is_closing = 1;
   }
}
static bool send_command(struct kiwi_instance *k, const char *command) {
   if (!k->connection || !k->connection->is_websocket || k->connection->is_closing || k->connection->send.len > KIWI_QUEUE_MAX) {
      return false;
   }

   return mg_ws_send(k->connection, command, strlen(command), WEBSOCKET_OP_TEXT) != 0;
}
static void *kiwi_create(const rrp_sdr_host_t *host) {
   struct kiwi_instance *k = host ? calloc(1, sizeof(*k)) : NULL;

   if (!k) {
      return NULL;
   }
   k->host = *host;
   k->max_hz = 30000000;
   k->hz = k->wanted_hz = 14074000;
   strcpy(k->mode, "USB");
   strcpy(k->wanted_mode, "USB");
   mg_mgr_init(&k->manager);

   return k;
}
static void kiwi_destroy(void *instance) {
   struct kiwi_instance *k = instance;
   k->closing = true;
   mg_mgr_free(&k->manager);
   free(k);
}
static bool kiwi_capabilities(void *instance, rrp_sdr_capabilities_t *out) {
   if (!instance || !out) {
      return false;
   }
   *out = (rrp_sdr_capabilities_t) {
      .rx = true, .rx_audio = true, .rx_min_hz = 0, .rx_max_hz = ((struct kiwi_instance *)instance)->max_hz, .rx_modes = "USB LSB AM CW"
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
   char tune[160];

   if (!kiwi_tune(k, k->wanted_hz, k->wanted_mode, tune, sizeof(tune)) ||
      !send_command(k, "SET compression=0") || !send_command(k, "SET squelch=0 max=0") ||
      !send_command(k, "SET genattn=0") || !send_command(k, "SET gen=0 mix=-1") ||
      !send_command(k, "SET agc=1 hang=0 thresh=-100 slope=6 decay=1000 manGain=50") || !send_command(k, tune)) {
      return false;
   }
   k->configured = true;
   k->deadline = mono_us() + 5000000;

   return true;
}
static void receiver_metadata(struct kiwi_instance *k, const char *encoded) {
   if (strlen(encoded) > 60000) {
      return;
   }
   char *text = malloc(strlen(encoded) + 1);

   if (!text) {
      return;
   }
   int length = mg_url_decode(encoded, strlen(encoded), text, strlen(encoded) + 1, 0);

   if (length >= 0) {
      struct mg_str json = mg_str_n(text, length);
      long api = mg_json_get_long(json, "$.ext_api", -1);

      if (api >= 0 && api <= 1000000) {
         char allowance[32];
         snprintf(allowance, sizeof(allowance), "%ld", api);

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
         char path[64];
         snprintf(path, sizeof(path), "$.%s", remote[i]);
         char *value = mg_json_get_str(json, path);

         if (value && *value && k->host.metadata) {
            k->host.metadata(k->host.user, local[i], value);
         }
         free(value);
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
/* Only inspect a complete response header, never arbitrary response text.
 * Redirects are connection attempts too; no loops or unlimited retries. */
static bool kiwi_redirect(struct kiwi_instance *k, struct mg_connection *c) {
   if (c->is_websocket || c->recv.len < 12 || c->recv.len > 8192 ||
       memcmp(c->recv.buf, "HTTP/", 5)) return false;
   const char *buf = (const char *)c->recv.buf;
   const char *end = buf + c->recv.len;
   const char *headers_end = NULL;
   for (const char *p = buf; p + 3 < end; p++)
      if (!memcmp(p, "\r\n\r\n", 4)) { headers_end = p; break; }
   if (!headers_end || end - buf < 12 || memcmp(buf + 9, "307", 3)) return false;
   const char *loc = NULL;
   size_t len = 0;
   for (const char *p = buf; p < headers_end;) {
      const char *line = NULL;
      for (const char *scan = p; scan + 1 < headers_end + 2; scan++) {
         if (scan[0] == '\r' && scan[1] == '\n') { line = scan; break; }
      }
      if (!line || line > headers_end) break;
      if (line - p >= 9 && !strncasecmp(p, "Location:", 9)) {
         loc = p + 9;
         while (loc < line && (*loc == ' ' || *loc == '\t')) loc++;
         len = (size_t)(line - loc);
         break;
      }
      p = line + 2;
   }
   if (k->attempts >= 3) {
      note(k, "Kiwi HTTP 307 received but the three-attempt connection limit was reached");
      return false;
   }
   if (!loc || !len || len >= sizeof(k->redirect_url)) return false;
   char target[512];
   memcpy(target, loc, len); target[len] = 0;
   for (size_t i = 0; i < len; i++)
      if ((unsigned char)target[i] < 33 || (unsigned char)target[i] > 126) return false;
   char next[512];
   if (!strncasecmp(target, "https://", 8))
      snprintf(next, sizeof(next), "wss://%s", target + 8);
   else if (!strncasecmp(target, "http://", 7))
      snprintf(next, sizeof(next), "ws://%s", target + 7);
   else if (!strncasecmp(target, "wss://", 6) || !strncasecmp(target, "ws://", 5))
      snprintf(next, sizeof(next), "%s", target);
   else if (*target == '/') {
      const char *path = strstr(k->url, "://");
      if (!path) return false;
      path = strchr(path + 3, '/');
      size_t authority = path ? (size_t)(path - k->url) : strlen(k->url);
      if (authority + len >= sizeof(next)) return false;
      memcpy(next, k->url, authority);
      memcpy(next + authority, target, len + 1);
   } else return false;
   if (strlen(next) >= sizeof(k->url)) return false;
   if (!strcmp(next, k->url)) {
      note(k, "Kiwi HTTP 307 redirects to the same WebSocket URL (redirect loop)");
      return false;
   }
   /* Never forward configured credentials to a different origin. */
   const char *password = setting(k, "password");
   if (password && *password) {
      const char *old = strstr(k->url, "://"), *dest = strstr(next, "://");
      if (!old || !dest) return false;
      old += 3; dest += 3;
      size_t olen = strcspn(old, "/"), dlen = strcspn(dest, "/");
      if (olen != dlen || strncasecmp(old, dest, olen)) return false;
   }
   /* This is a diagnostic for the peer redirect, not a credential dump. */
   const char *authority = strstr(next, "://");
   const char *path = authority ? strchr(authority + 3, '/') : NULL;
   char origin[160] = "";
   if (authority) {
      size_t length = path ? (size_t)(path - next) : strlen(next);
      snprintf(origin, sizeof(origin), "%.*s", (int)(length < sizeof(origin) - 1 ? length : sizeof(origin) - 1), next);
   }
   char diagnostic[240];
   snprintf(diagnostic, sizeof(diagnostic), "Kiwi HTTP 307: redirect attempt %u/3 to %.150s (path length %zu)",
      k->attempts + 1, origin, path ? strlen(path) : 0u);
   note(k, diagnostic);
   snprintf(k->redirect_url, sizeof(k->redirect_url), "%s", next);
   k->redirecting = true;
   c->is_closing = 1;
   return true;
}
static void websocket(struct mg_connection *c, int event, void *data) {
   struct kiwi_instance *k = c->fn_data;

   if (event == MG_EV_WS_OPEN) {
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
   } else if (event == MG_EV_WS_MSG && !k->failed) {
      struct mg_ws_message *m = data;

      if (!kiwi_receive(k, m->data.buf, m->data.len)) {
         fail(k, "Invalid or unsupported Kiwi stream");
      }
   } else if (event == MG_EV_READ && !k->redirecting && kiwi_redirect(k, c)) {
      /* The next connection uses Location after this socket closes. */
   } else if (event == MG_EV_READ && c->recv.len > 65536) {
      fail(k, "Kiwi WebSocket receive limit exceeded");
   } else if (event == MG_EV_ERROR && !k->redirecting && kiwi_redirect(k, c)) {
      /* The WebSocket handshake parser can raise ERROR before the application
       * receives READ. Inspect the retained HTTP response here as well. */
   } else if (event == MG_EV_ERROR && !k->redirecting) {
      /* Mongoose reports any non-101 HTTP response as "ws handshake error".
       * Keep only the numeric status; never log peer response bodies/headers
       * that could contain credentials or arbitrary text. */
      if (!c->is_websocket && c->recv.len >= 12 &&
          !memcmp(c->recv.buf, "HTTP/", 5)) {
         const char *p = (const char *) c->recv.buf;
         const char *end = p + c->recv.len;
         const char *space = memchr(p, ' ', (size_t)(end - p));
         if (space && end - space >= 4 &&
             isdigit((unsigned char)space[1]) &&
             isdigit((unsigned char)space[2]) &&
             isdigit((unsigned char)space[3])) {
            char reason[96];
            if (!memcmp(space + 1, "307", 3))
               snprintf(reason, sizeof(reason), "%s",
                  "Kiwi HTTP 307 redirect could not be followed (missing, invalid, or unsupported Location)");
            else
               snprintf(reason, sizeof(reason), "Kiwi WebSocket handshake rejected (HTTP %.3s)", space + 1);
            fail(k, reason);
         } else {
            fail(k, "Kiwi WebSocket connection failed");
         }
      } else {
         fail(k, "Kiwi WebSocket connection failed");
      }
   } else if (event == MG_EV_CLOSE && c == k->connection) {
      k->connection = NULL;

      if (k->redirecting) {
         k->redirecting = false;
         snprintf(k->url, sizeof(k->url), "%s", k->redirect_url);
         k->retry = mono_us();
         note(k, "Kiwi WebSocket redirected (HTTP 307); following Location");
      } else if (!k->closing) {
         bool was_online = k->online;
         unavailable(k);
         if (was_online)
            note(k, "Kiwi WebSocket connection lost after receiver was online");
         if (k->attempts >= 3) {
            note(k, "Kiwi receiver stopped after 3 connection attempts");
            k->retry = UINT64_MAX;
            return;
         }
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
   // Same timestamped SND route as kiwiclient, with a unique process suffix.
   snprintf(k->url, sizeof(k->url), "ws://%s:%lu/%" PRIu64 "/SND", host, port, (uint64_t) time(NULL) * 1000 + (unsigned)getpid() % 1000);

   if (k->host.metadata) {
      char endpoint[320];
      snprintf(endpoint, sizeof(endpoint), "http://%s:%lu/", host, port);
      k->host.metadata(k->host.user, "url", endpoint);
   }
   k->started = true;

   return true;
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
      char headers[416];
      snprintf(headers, sizeof(headers), "User-Agent: %s\r\n", ua);
      k->connection = mg_ws_connect(&k->manager, k->url, websocket, k, "%s", headers);
      k->deadline = current + 10000000;
      k->received = current;
      k->keepalive = current;

      if (!k->connection) {
         k->retry = current + 1000000;
      }
   }
   mg_mgr_poll(&k->manager, 0);
   current = mono_us();

   if (!k->connection) {
      return;
   }



   if ((!k->online && current >= k->deadline) || (k->request && current >= k->deadline) || current - k->received > 10000000) {
      /* A media subscription only proves the RustyRig encoder is ready.
       * Distinguish remote handshake, configuration, tune and stream stalls. */
      char reason[192];
      snprintf(reason, sizeof(reason),
         "Kiwi receiver timed out (%s; ws=%s auth=%s configured=%s online=%s rate=%u; last-audio=%" PRIu64 "ms)",
         k->request ? "tune acknowledgement" :
         !k->connection->is_websocket ? "WebSocket handshake" :
         !k->authenticated ? "authentication" :
         !k->rate ? "sample-rate announcement" :
         !k->configured ? "receiver configuration" :
         !k->online ? "first tuned audio packet" : "audio stream stalled",
         k->connection->is_websocket ? "yes" : "no",
         k->authenticated ? "yes" : "no", k->configured ? "yes" : "no",
         k->online ? "yes" : "no", k->rate,
         (uint64_t)((current - k->received) / 1000));
      fail(k, reason);

      return;
   }

   if (k->connection->is_websocket && current - k->keepalive >= 1000000) {
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
   k->request = request;
   k->deadline = mono_us() + 4000000;

   return true;
}

static const rrp_sdr_driver_t kiwi_driver = {
   .abi_version = RRP_SDR_ABI_VERSION, .name = "kiwi", .create = kiwi_create,
   .destroy = kiwi_destroy, .capabilities = kiwi_capabilities, .start = kiwi_start, .poll = kiwi_poll,
   .set_receiver = kiwi_set_receiver, .tune = kiwi_tune, .receive = kiwi_receive
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
