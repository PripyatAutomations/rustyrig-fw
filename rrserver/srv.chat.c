//
// ws.chat.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <rrserver/srv.chat.internal.h>

// minimum reason length for kick/ban/etc
#define CHAT_MIN_REASON_LEN 10
#define CALLSIGN_LOOKUP_START_TIMEOUT_MS 60000

extern time_t now;
extern rrconn_t *http_client_list;
extern bool dying, restarting;
extern const char *config_file;
extern bool ws_chat_err_noprivs(rrconn_t *cptr, const char *action);
extern bool ws_chat_error_need_reason(rrconn_t *cptr, const char *command);

static subproc_t callsign_lookup_process = SUBPROC_INIT;

static bool callsign_lookup_atexit_registered = false;
static bool callsign_lookup_ready = false;
static bool callsign_lookup_pending = false;
static rrconn_t *callsign_lookup_client = NULL;
static time_t callsign_lookup_deadline = 0;
static char callsign_lookup_request_text[256];
static char callsign_lookup_reply[HTTP_WS_MAX_MSG];
static size_t callsign_lookup_reply_len = 0;

static void callsign_lookup_stop(void) {
   subproc_stop(&callsign_lookup_process, SIGTERM);
   callsign_lookup_ready = false;
   callsign_lookup_pending = false;
   callsign_lookup_client = NULL;
   callsign_lookup_deadline = 0;
   callsign_lookup_request_text[0] = '\0';
   callsign_lookup_reply_len = 0;
}

static bool callsign_lookup_readline(char *line, size_t len, int timeout_ms) {
   return subproc_read_line(&callsign_lookup_process, line, len, timeout_ms);
}

static bool callsign_lookup_wait_ready(int timeout_ms) {
   if (callsign_lookup_ready) {
      return true;
   }

   if (callsign_lookup_process.pid <= 0 || !callsign_lookup_process.input || !callsign_lookup_process.output) {
      return false;
   }

   char line[1024];
   while (callsign_lookup_readline(line, sizeof(line), timeout_ms)) {
      if (strncmp(line, "+OK ", 4) == 0) {
         callsign_lookup_ready = true;
         Log(LOG_INFO, "callsign", "lookup helper ready (pid %ld)", (long)callsign_lookup_process.pid);

         return true;
      }
      line[strcspn(line, "\r\n")] = '\0';

      /* The helper may load the shared server configuration without implementing every section. Do not promote routine parser output or its protocol banner
       * into rrserver warnings. */
      if (*line && (strstr(line, "<cfg@debug>") || strstr(line, "<cfg@info>") ||
         strstr(line, "[Debug] config ") || !strncmp(line, "+NOTICE", 7) ||
         !strncmp(line, "+PROTO", 6) || strstr(line, "---- Log Filters ----") ||
         !strcmp(line, "---------------------") || strstr(line, " = 3 (info)") || strstr(line, " = 4 (debug)"))) {
         continue;
      }

      if (*line) {
         Log(LOG_WARN, "callsign", "lookup startup: %s", line);
      }
   }

   /* A zero-time probe is used from the WebSocket event loop.  No data yet means only that startup is still in progress; preserve the helper and let a later
    * request probe it again. */
   if (timeout_ms == 0) {
      int status = 0;
      bool ended = subproc_reap(&callsign_lookup_process, &status);

      if (ended) {
         if (WIFEXITED(status)) {
            Log(LOG_WARN, "callsign", "lookup exited before ready (status %d)", WEXITSTATUS(status));
         } else if (WIFSIGNALED(status)) {
            Log(LOG_WARN, "callsign", "lookup terminated before ready by signal %d", WTERMSIG(status));
         }
         callsign_lookup_stop();
      }

      return false;
   }

   int status = 0;
   bool ended = subproc_reap(&callsign_lookup_process, &status);

   if (ended) {
      if (WIFEXITED(status)) {
         Log(LOG_WARN, "callsign", "lookup exited before ready (status %d)", WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
         Log(LOG_WARN, "callsign", "lookup terminated before ready by signal %d", WTERMSIG(status));
      }
   } else {
      char *program = cfg_get_path("callsign-lookup:path");
      Log(LOG_WARN, "callsign", "lookup did not report ready within %d seconds (program=%s, config=%s)", (timeout_ms + 999) / 1000, (program && *program) ?
         program : "(unset)", (config_file && *config_file) ? config_file : "(unset)");
      free(program);
   }
   callsign_lookup_stop();

   return false;
}

static bool callsign_lookup_send_reply(rrconn_t *cptr, const char *text) {
   if (!cptr || !text) {
      return false;
   }
   dict *message = dict_new();

   if (!message) {
      return false;
   }
   dict_add(message, "msg.type", "callsign");
   dict_add_ulong(message, "msg.ts", now);
   /* Keep the wire response machine-readable.  The dictionary serializer turns dotted keys into nested JSON objects. */
   const char *line = text;
   bool first = true;
   while (*line) {
      const char *end = strchr(line, '\n');
      size_t line_len = end ? (size_t)(end - line) : strlen(line);

      if (line_len > 0) {
         const char *colon = memchr(line, ':', line_len);

         if (first) {
            char status[256];
            size_t n = line_len < sizeof(status) - 1 ? line_len : sizeof(status) - 1;
            memcpy(status, line, n);
            status[n] = '\0';
            dict_add(message, "callsign.status", status);
            first = false;
         } else if (colon && colon > line) {
            char key[128];
            size_t key_len = (size_t)(colon - line);

            if (key_len >= sizeof(key)) {
               key_len = sizeof(key) - 1;
            }
            size_t out = 0;

            for (size_t i = 0 ; i < key_len && out + 1 < sizeof(key) ; i++) {
               unsigned char ch = (unsigned char)line[i];

               if (isalnum(ch)) {
                  key[out++] = (char)tolower(ch);
               } else if (out > 0 && key[out - 1] != '_') {
                  key[out++] = '_';
               }
            }

            while (out > 0 && key[out - 1] == '_') {
               out--;
            }
            key[out] = '\0';

            if (out > 0) {
               char field_key[160];
               snprintf(field_key, sizeof(field_key), "callsign.fields.%s", key);
               char value[1024];
               size_t value_len = line_len - (size_t)(colon - line) - 1;

               if (value_len >= sizeof(value)) {
                  value_len = sizeof(value) - 1;
               }
               memcpy(value, colon + 1, value_len);
               value[value_len] = '\0';
               while (*value == ' ') {
                  memmove(value, value + 1, strlen(value));
               }
               dict_add(message, field_key, value);
            }
         }
      }

      if (!end) {
         break;
      }
      line = end + 1;
   }
   dict_add_bool(message, "callsign.done", true);
   bool sent = ws_send_dict(NULL, cptr, message, WEBSOCKET_OP_TEXT);
   dict_free(message);

   return sent;
}

static bool callsign_lookup_start(void) {
   if (callsign_lookup_process.pid > 0 && callsign_lookup_process.input && callsign_lookup_process.output) {
      return true;
   }

   char *program = cfg_get_path("callsign-lookup:path");

   if (!program || !*program || !config_file || !*config_file) {
      Log(LOG_WARN, "callsign", "Cannot start lookup: callsign-lookup:path or server config is missing (program=%s, config=%s)", (program && *program) ? program
         : "(unset)", (config_file && *config_file) ? config_file : "(unset)");
      free(program);

      return false;
   }

   if (access(program, X_OK) != 0) {
      Log(LOG_WARN, "callsign", "Cannot start lookup: %s is not executable: %s", program, strerror(errno));
      free(program);

      return false;
   }

   if (access(config_file, R_OK) != 0) {
      Log(LOG_WARN, "callsign", "Cannot start lookup: config %s is not readable: %s", config_file, strerror(errno));
      free(program);

      return false;
   }

   if (!callsign_lookup_atexit_registered) {
      atexit(callsign_lookup_stop);
      callsign_lookup_atexit_registered = true;
   }

   const char *argv[] = {
      program, "-q", "-f", config_file, NULL
   };

   if (!subproc_spawn(&callsign_lookup_process, program, argv, true)) {
      Log(LOG_WARN, "callsign", "Cannot start lookup: subprocess setup failed for %s: %s", program, strerror(errno));
      callsign_lookup_stop();
      free(program);

      return false;
   }
   free(program);

   return true;
}

// Start the persistent lookup helper during server initialization rather
// than making the first /qrz or /grid request pay the startup cost.
bool ws_callsign_lookup_init(void) {
   if (!callsign_lookup_start()) {
      return false;
   }

   /* The helper is launched before the network listener.  Give its banner a bounded startup window here so the first request cannot race readiness, while
    * keeping the old unbounded 60-second startup hang impossible. */
   if (!callsign_lookup_wait_ready(5000)) {
      Log(LOG_WARN, "callsign", "lookup helper did not become ready during initialization");
   }

   return callsign_lookup_ready;
}

/* Drain helper startup output from the server's periodic tick without ever blocking the WebSocket/media event loop. */
void ws_callsign_lookup_poll(void) {
   if (callsign_lookup_process.pid > 0 && !callsign_lookup_ready) {
      (void)callsign_lookup_wait_ready(0);
   }

   if (!callsign_lookup_pending || !callsign_lookup_ready) {
      return;
   }

   char line[1024];
   while (callsign_lookup_readline(line, sizeof(line), 0)) {
      line[strcspn(line, "\r\n")] = '\0';

      if (!*line || strncmp(line, "+NOTICE ", 8) == 0 ||
         strncmp(line, "+OK ", 4) == 0 || strncmp(line, "+PROTO ", 7) == 0 ||
         strncmp(line, "+GOODBYE", 8) == 0 || line[0] == '[' || line[0] == '<' || strncmp(line, "==", 2) == 0) {
         continue;
      }

      if (strcmp(line, "+EOR") == 0) {
         callsign_lookup_reply[callsign_lookup_reply_len] = '\0';

         if (callsign_lookup_client) {
            callsign_lookup_send_reply(callsign_lookup_client, callsign_lookup_reply);
         }
         callsign_lookup_pending = false;
         callsign_lookup_client = NULL;
         callsign_lookup_deadline = 0;
         callsign_lookup_request_text[0] = '\0';

         return;
      }
      size_t line_len = strlen(line);

      if (callsign_lookup_reply_len + line_len + 2 < sizeof(callsign_lookup_reply)) {
         memcpy(callsign_lookup_reply + callsign_lookup_reply_len, line, line_len);
         callsign_lookup_reply_len += line_len;
         callsign_lookup_reply[callsign_lookup_reply_len++] = '\n';
      }
   }

   if (callsign_lookup_deadline > 0 && now >= callsign_lookup_deadline) {
      if (callsign_lookup_client) {
         ws_send_error(callsign_lookup_client, "Callsign lookup timed out");
      }
      Log(LOG_WARN, "callsign", "lookup helper did not finish request: %s", callsign_lookup_request_text);
      callsign_lookup_stop();
   }
}

static bool callsign_lookup_request(rrconn_t *cptr, const char *request) {
   if (callsign_lookup_pending) {
      return false;
   }

   if (!callsign_lookup_start()) {
      return false;
   }

   /* This runs on the WebSocket/event-loop thread.  Allow only a short grace period for the startup banner to cross the pipe; the long startup wait must remain
    * outside request handling so media cannot be stalled. */
   if (!callsign_lookup_wait_ready(250)) {
      Log(LOG_INFO, "callsign", "lookup helper is still starting; request will need to be retried");

      return false;
   }

   if (!subproc_write_line(&callsign_lookup_process, request)) {
      Log(LOG_WARN, "callsign", "lookup request failed while writing to helper (pid %ld): %s", (long)callsign_lookup_process.pid, strerror(errno));
      callsign_lookup_stop();

      return false;
   }
   snprintf(callsign_lookup_request_text, sizeof(callsign_lookup_request_text), "%s", request);
   callsign_lookup_client = cptr;
   callsign_lookup_pending = true;
   callsign_lookup_deadline = now + 30;
   callsign_lookup_reply_len = 0;

   return true;
}

typedef struct {
   char name[128];
   bool has_vfos;
   uint32_t vfo_mask;
   uint32_t rx_tuning_mask;
} ws_room_meta_t;

static ws_room_meta_t room_meta[32];
static uint32_t authoritative_vfo_mask = 3;
static const char *room_canonical(const char *room);
static char authoritative_room_override[128];

static ws_room_meta_t *room_meta_find(const char *room, bool create) {
   const char *canonical = room_canonical(room);

   for (size_t i = 0 ; i < sizeof(room_meta) / sizeof(room_meta[0]) ; i++) {
      if (room_meta[i].name[0] && strcasecmp(room_meta[i].name, canonical) == 0) {
         return &room_meta[i];
      }
   }

   if (!create) {
      return NULL;
   }

   for (size_t i = 0 ; i < sizeof(room_meta) / sizeof(room_meta[0]) ; i++) {
      if (!room_meta[i].name[0]) {
         snprintf(room_meta[i].name, sizeof(room_meta[i].name), "%s", canonical);

         return &room_meta[i];
      }
   }

   return NULL;
}

bool ws_room_name_valid(const char *room) {
   if (!room || (room[0] != '#' && room[0] != '&') || !room[1] || strlen(room) >= sizeof(room_meta[0].name)) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)room ; *p ; p++) {
      if (isspace(*p) || *p == ',' || *p < 32 || *p == 127) {
         return false;
      }
   }

   return true;
}

const char *ws_site_room(void) {
   static char room[128];
   char *configured = (char *)cfg_get_exp("station.name");
   int length = snprintf(room, sizeof(room), "#%s", configured && *configured ? configured : "rustyrig");
   free(configured);

   if (length < 0 || (size_t)length >= sizeof(room)) {
      room[0] = '\0';
   }

   return room;
}

/* PARITY: rustyrig-fw/rrserver/events.c:rrserver_room_join_check Reserve dashed names for present and future linked station namespaces. Also protect this
 * server's lobby, which has no dash. */
bool ws_room_station_scoped(const char *room) {
   return ws_room_name_valid(room) && (strchr(room + 1, '-') || !strcasecmp(room, ws_site_room()));
}

static rr_vfo_t ws_chat_selected_vfo(const rrconn_t *client, const char *room) {
   uint32_t mask = ws_room_vfo_mask(room);

   if (client->chat_vfo && !strcasecmp(client->chat_vfo_room, room)) {
      rr_vfo_t selected = vfo_lookup(client->chat_vfo);

      if (selected >= 0 && selected < 32 && (mask & (UINT32_C(1) << selected))) {
         return selected;
      }
   }

   for (rr_vfo_t index = 0 ; index < MAX_VFOS && index < 32 ; index++) {
      if (mask & (UINT32_C(1) << index)) {
         return index;
      }
   }

   return VFO_NONE;
}

// Base rig rooms are reserved even outside this site's namespace.
bool ws_room_rig_base(const char *room) {
   if (!ws_room_name_valid(room)) {
      return false;
   }
   const char *dash = strrchr(room, '-');

   if (!dash || strncasecmp(dash, "-rig", 4) || !isdigit((unsigned char)dash[4])) {
      return false;
   }
   const char *p = dash + 4;
   while (isdigit((unsigned char)*p)) {
      p++;
   }
   return !*p;
}

bool ws_room_rig_namespace(const char *room) {
   if (!ws_room_name_valid(room) || room[0] != '#') {
      return false;
   }
   const char *site = ws_site_room();
   size_t prefix = strlen(site);

   if (strncasecmp(room, site, prefix) || strncasecmp(room + prefix, "-rig", 4)) {
      return false;
   }
   const char *p = room + prefix + 4;

   if (!isdigit((unsigned char)*p)) {
      return false;
   }
   while (isdigit((unsigned char)*p)) {
      p++;
   }
   return !*p || (*p == '.' && p[1]);
}

bool ws_room_same_rig(const char *room, const char *base) {
   if (!room || !base || !ws_room_rig_base(base)) {
      return false;
   }
   size_t n = strlen(base);

   return !strncasecmp(room, base, n) && (!room[n] || (room[n] == '.' && room[n + 1]));
}

bool ws_room_tx_control(const char *room) {
   return ws_room_rig_namespace(room) && ws_room_rig_base(room) && room_meta_find(room, false);
}

uint32_t ws_room_rx_tuning_mask(const char *room) {
   if (!ws_room_rig_namespace(room)) {
      return 0;
   }

   for (size_t i = 0 ; i < sizeof(room_meta) / sizeof(room_meta[0]) ; i++) {
      if (room_meta[i].rx_tuning_mask && ws_room_same_rig(room, room_meta[i].name)) {
         return room_meta[i].rx_tuning_mask & ws_room_vfo_mask(room);
      }
   }

   return 0;
}
bool ws_room_rx_tunable(const char *room) {
   /* SDR rooms expose a single attached RX-only VFO, not a rig TX room. */
   if (room && strlen(room) == 13 && !strncasecmp(room, "#sdr-", 5)) {
      return (ws_room_vfo_mask(room) & UINT32_C(1)) != 0;
   }

   return ws_room_rx_tuning_mask(room) != 0;
}
bool ws_room_set_rx_tuning_mask(const char *room, uint32_t mask) {
   if (!ws_room_tx_control(room) || (mask & ~ws_room_vfo_mask(room))) {
      return false;
   }
   room_meta_find(room, false)->rx_tuning_mask = mask;

   return true;
}
bool ws_room_set_rx_tuning(const char *room, bool enabled) {
   return ws_room_set_rx_tuning_mask(room, enabled ? ws_room_vfo_mask(room) : 0);
}

bool ws_room_control_allowed(rrconn_t *client, const char *room, bool frequency) {
   return client && ws_client_in_room(client, room) && (ws_room_tx_control(room) || (frequency && ws_room_has_vfos(room) && ws_room_rx_tunable(room)));
}

bool ws_room_set_vfo_mask(const char *room, uint32_t mask) {
   if (!ws_room_name_valid(room) || (mask && !ws_room_rig_namespace(room) && !(strlen(room) == 13 && !strncasecmp(room, "#sdr-", 5) && mask == UINT32_C(1)))) {
      return false;
   }
   ws_room_meta_t *meta = room_meta_find(room, true);

   if (!meta) {
      return false;
   }
   meta->has_vfos = mask != 0;
   meta->vfo_mask = mask;

   return true;
}

const char *ws_authoritative_room(void) {
   static char room[128];

   if (authoritative_room_override[0]) {
      return authoritative_room_override;
   }
   char *configured = (char *)cfg_get_exp("station.name");
   const char *name = (configured && *configured) ? configured : "rustyrig";
   snprintf(room, sizeof(room), "#%s-rig0", name);
   free(configured);
   ws_room_meta_t *meta = NULL;

   for (size_t i = 0 ; i < sizeof(room_meta) / sizeof(room_meta[0]) ; i++) {
      if (strcasecmp(room_meta[i].name, room) == 0) {
         meta = &room_meta[i];
         break;
      }

      if (!meta && !room_meta[i].name[0]) {
         snprintf(room_meta[i].name, sizeof(room_meta[i].name), "%s", room);
         meta = &room_meta[i];
         break;
      }
   }

   if (meta) {
      meta->has_vfos = ws_room_rig_namespace(room) && authoritative_vfo_mask != 0;
      meta->vfo_mask = meta->has_vfos ? authoritative_vfo_mask : 0;
   }

   return room;
}

void ws_set_authoritative_room(const char *room) {
   if (!ws_room_name_valid(room)) {
      return;
   }
   snprintf(authoritative_room_override, sizeof(authoritative_room_override), "%s", room);
}

bool ws_room_has_vfos(const char *room) {
   ws_room_meta_t *meta = room_meta_find(room, false);

   return meta ? meta->has_vfos : false;
}

uint32_t ws_room_vfo_mask(const char *room) {
   ws_room_meta_t *meta = room_meta_find(room, false);

   return meta ? meta->vfo_mask : 0;
}

void ws_set_authoritative_vfo_mask(uint32_t mask) {
   authoritative_vfo_mask = mask;
   ws_room_meta_t *meta = room_meta_find(ws_authoritative_room(), true);

   if (meta) {
      meta->has_vfos = mask != 0;
      meta->vfo_mask = mask;
   }
}

static const char *room_canonical(const char *room) {
   return (room && *room) ? room : ws_authoritative_room();
}

bool ws_client_in_room(const rrconn_t *cptr, const char *room) {
   if (!cptr) {
      return false;
   }
   const char *want = room_canonical(room);
   char copy[AUTOJOIN_LEN];
   snprintf(copy, sizeof(copy), "%s", cptr->rooms);
   char *save = NULL;

   for (char *tok = strtok_r(copy, ",", &save) ; tok ; tok = strtok_r(NULL, ",", &save)) {
      if (strcasecmp(tok, want) == 0) {
         return true;
      }
   }

   return false;
}

/* SDR receiver tuning belongs to the first still-present room member. Keep JOIN order separate from connection age; PART and reconnect relinquish it. */
typedef struct sdr_member {
   struct sdr_member *next;
   rrconn_t *client;
   char room[64];
} sdr_member_t;
static sdr_member_t *sdr_members, **sdr_members_tail = &sdr_members;
static bool sdr_room(const char *room) {
   return room && strlen(room) == 13 && !strncasecmp(room, "#sdr-", 5);
}
static void sdr_member_remove(rrconn_t *client, const char *room) {
   sdr_member_t **slot = &sdr_members;
   while (*slot) {
      sdr_member_t *member = *slot;

      if (member->client == client && (!room || !strcasecmp(member->room, room))) {
         *slot = member->next;

         if (!*slot) {
            sdr_members_tail = slot;
         }
         free(member);
      } else {
         slot = &member->next;
      }
   }
}
bool ws_room_sdr_tuning_owner(rrconn_t *client, const char *room) {
   if (!client || !sdr_room(room) || !ws_client_in_room(client, room)) {
      return false;
   }
   sdr_member_t **slot = &sdr_members;
   while (*slot) {
      sdr_member_t *member = *slot;
      bool live = false;

      for (rrconn_t *c = http_client_list ; c ; c = c->next) {
         if (c == member->client && ws_client_in_room(c, member->room)) {
            live = true;
            break;
         }
      }

      if (!live) {
         *slot = member->next;

         if (!*slot) {
            sdr_members_tail = slot;
         }
         free(member);
         continue;
      }

      if (!strcasecmp(member->room, room)) {
         return member->client == client;
      }
      slot = &member->next;
   }
   return false;
}
bool ws_client_join_room(rrconn_t *cptr, const char *room) {
   if (!cptr || !ws_room_name_valid(room)) {
      return false;
   }
   const char *canonical = room_canonical(room);

   if (ws_room_rig_base(canonical) && !room_meta_find(canonical, false)) {
      return false;
   }

   if (canonical[0] != '#' && canonical[0] != '&') {
      return false;
   }

   if (ws_client_in_room(cptr, canonical)) {
      return true;
   }
   rr_room_join_check_t check = {
      .room = canonical, .allowed = true
   };
   event_emit_binary(RR_ROOM_JOIN_CHECK_EVENT, cptr, &check, sizeof(check));

   if (!check.allowed) {
      return false;
   }
   size_t used = strlen(cptr->rooms);
   size_t need = strlen(canonical) + (used ? 1 : 0);

   if (used + need + 1 >= sizeof(cptr->rooms)) {
      return false;
   }

   if (used) {
      strlcat(cptr->rooms, ",", sizeof(cptr->rooms));
   }
   strlcat(cptr->rooms, canonical, sizeof(cptr->rooms));

   if (sdr_room(canonical)) {
      sdr_member_t *member = calloc(1, sizeof(*member));

      if (member) {
         member->client = cptr;
         snprintf(member->room, sizeof(member->room), "%s", canonical);
         *sdr_members_tail = member;
         sdr_members_tail = &member->next;
      }
   }

   return true;
}

bool ws_client_part_room(rrconn_t *cptr, const char *room) {
   if (!cptr || !room || strcasecmp(room, ws_site_room()) == 0) {
      return false;
   }
   const char *want = room_canonical(room);
   char old[AUTOJOIN_LEN], out[AUTOJOIN_LEN] = "";
   snprintf(old, sizeof(old), "%s", cptr->rooms);
   char *save = NULL;

   for (char *tok = strtok_r(old, ",", &save) ; tok ; tok = strtok_r(NULL, ",", &save)) {
      if (strcasecmp(tok, want) == 0) {
         continue;
      }

      if (out[0]) {
         strlcat(out, ",", sizeof(out));
      }
      strlcat(out, tok, sizeof(out));
   }

   snprintf(cptr->rooms, sizeof(cptr->rooms), "%s", out);

   if (sdr_room(want)) {
      sdr_member_remove(cptr, want);
   }
   media_part_room(cptr, room);
   event_emit("room.part", cptr, room);
   media_send_available_all(cptr);

   return true;
}

void ws_broadcast_room_dict(rrconn_t *sender, dict *d, const char *room) {
   if (d) {
      rr_server_publish_t publish = {
         .message = d, .room = room
      };
      event_emit_binary(RR_SERVER_PUBLISH_EVENT, sender, &publish, sizeof(publish));
   }

   if (!d) {
      return;
   }
   rrconn_t *cur = http_client_list;
   while (cur) {
      if (cur->is_ws && cur->authenticated && ws_client_in_room(cur, room)) {
         ws_send_dict(sender, cur, d, WEBSOCKET_OP_TEXT);
      }
      cur = cur->next;
   }
}

///////////////////////////////
// DIE: Makes the server die //
///////////////////////////////
static bool ws_chat_cmd_die(rrconn_t *cptr, const char *reason) {
   if (!cptr) {
      return true;
   }

   if (!reason || strlen(reason) < CHAT_MIN_REASON_LEN) {
      ws_chat_error_need_reason(cptr, "die");

      return true;
   }

   if (!cptr->user) {
      return true;
   }

   if (has_priv(cptr->user->uid, "admin|owner")) {
      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG + 1];
      prepare_msg(msgbuf, sizeof(msgbuf), "Shutting down due to /die \"%s\" from %s (uid: %d with privs %s)", (reason ? reason : "No reason given"), cptr->
         chatname, cptr->user->uid, cptr->user->privs);
      send_global_alert("***SERVER***", msgbuf);
      // Throw a shutdown event
      event_emit("shutdown", NULL, NULL);

      // XXX: This should move to the shutdown event handler?
      dying = 1;
   } else {
      ws_chat_err_noprivs(cptr, "DIE");

      return true;
   }

   return false;
}

//////////////////////////////////////
// RESTART: Make the server restart //
//////////////////////////////////////
static bool ws_chat_cmd_restart(rrconn_t *cptr, const char *reason) {
   if (!cptr) {
      return true;
   }

   if (!reason || strlen(reason) < CHAT_MIN_REASON_LEN) {
      ws_chat_error_need_reason(cptr, "RESTART");

      return true;
   }

   if (!cptr->user) {
      return true;
   }

   if (has_priv(cptr->user->uid, "admin|owner")) {
      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG + 1];
      prepare_msg(msgbuf, sizeof(msgbuf), "Shutting down due to /restart from %s (uid: %d with privs %s): %s", cptr->chatname, cptr->user->uid, cptr->user->
         privs, reason);
      send_global_alert("***SERVER***", msgbuf);
      dying = 1;                // flag that this should be the last iteration
      restarting = 1;           // flag that we should restart after processing
      // the alert
   } else {
      ws_chat_err_noprivs(cptr, "RESTART");

      return true;
   }

   return false;
}

///////////////////////
// KICK: Kick a user //
///////////////////////
static bool ws_chat_cmd_kick(rrconn_t *cptr, const char *target, const char *reason) {
   if (!cptr) {
      return true;
   }

   if (!target) {
      // XXX: send an error response 'No target given'
      ws_send_error(cptr, "No target given for KICK");

      return true;
   }

   if (!reason || strlen(reason) < CHAT_MIN_REASON_LEN) {
      ws_chat_error_need_reason(cptr, "kick");

      return true;
   }

   if (has_priv(cptr->user->uid, "admin|owner")) {
      rrconn_t *acptr;
      int kicked = 0;

      for (acptr = http_client_list ; acptr ; acptr = acptr->next) {
         // skip this one as it's not a valid chat client
         if (!acptr->active || !acptr->is_ws || acptr->chatname[0] == '\0') {
            continue;
         }

         if (strcmp(acptr->chatname, target) == 0) {
            // Build and send message
            char msgbuf[HTTP_WS_MAX_MSG + 1];
            prepare_msg(msgbuf, sizeof(msgbuf), "kicked by %s (Reason: %s)", cptr->chatname, (reason ? reason : "No reason given"));
            Log(LOG_AUDIT, "admin.kick", "%s %s", acptr->chatname, msgbuf);
#ifdef  USE_MONGOOSE
            dict *notice = dict_new();

            if (notice) {
               dict_add(notice, "msg.type", "notice");
               dict_add(notice, "notice.msg", msgbuf);
               dict_add_ulong(notice, "msg.ts", now);
               ws_broadcast_dict_with_flags(FLAG_STAFF, NULL, notice, WEBSOCKET_OP_TEXT);
               dict_free(notice);
            }
            ws_kick_client(acptr, msgbuf);
#endif // USE_MONGOOSE
            kicked++;
         }
      }

      if (!kicked) {
         char msgbuf[HTTP_WS_MAX_MSG + 1];
         prepare_msg(msgbuf, sizeof(msgbuf), "KICK '%s' command matched no connected users", target);
         dict *err_msg = dict_new();
         dict_add(err_msg, "msg.type", "error");
         dict_add(err_msg, "error.msg", msgbuf);
         dict_add_ulong(err_msg, "error.ts", now);

         ws_send_dict(NULL, cptr, err_msg, WEBSOCKET_OP_TEXT);
         dict_free(err_msg);
      }
   } else {
      ws_chat_err_noprivs(cptr, "KICK");

      return true;
   }

   return false;
}

// Send the updated userinfo for a single user; see ws_send_users below for
// everyone
bool ws_send_userinfo_room(rrconn_t *cptr, rrconn_t *acptr, const char *room) {
   if (!cptr || !cptr->authenticated || !cptr->user) {
      return false;
   }
   const char *target_room = (room && *room) ? room : ws_authoritative_room();

   if (!target_room || !*target_room) {
      Log(LOG_WARN, "ws.chat", "Refusing userinfo for %s without a room", cptr->chatname[0] ? cptr->chatname : "<unknown>");

      return false;
   }

   // Video sources (webcam etc announcing hello.role: video-source) are not
   // users; keep them out of the chat/user lists
   if (client_has_flag(cptr, FLAG_VIDEO_SOURCE)) {
      return false;
   }
   dict *talk_msg = dict_new();
   dict_add(talk_msg, "msg.type", "talk");
   dict_add(talk_msg, "talk.privs", cptr->user->privs);
   dict_add(talk_msg, "talk.user", cptr->chatname);
   dict_add(talk_msg, "talk.room", target_room);
   dict_add(talk_msg, "talk.cmd", "userinfo");
   dict_add_int(talk_msg, "talk.sessions", cptr->user->sessions);
   dict_add_bool(talk_msg, "talk.muted", cptr->user->is_muted);
   dict_add_bool(talk_msg, "talk.tx", cptr->is_ptt);
   dict_add(talk_msg, "talk.ptt-room", cptr->ptt_room);

   if (cptr->ptt_vfo) {
      char ptt_vfo[2] = {
         cptr->ptt_vfo, '\0'
      };
      dict_add(talk_msg, "talk.ptt-vfo", ptt_vfo);
   }
   dict_add_long(talk_msg, "msg.ts", now);

   if (acptr) {
      ws_send_dict(NULL, acptr, talk_msg, WEBSOCKET_OP_TEXT);
   } else {
      ws_broadcast_dict(NULL, talk_msg, WEBSOCKET_OP_TEXT);
   }

   dict_free(talk_msg);

   return true;
}

bool ws_send_userinfo(rrconn_t *cptr, rrconn_t *acptr) {
   if (acptr) {
      return ws_send_userinfo_room(cptr, acptr, ws_authoritative_room());
   }

   if (!cptr) {
      return false;
   }
   char copy[AUTOJOIN_LEN];
   snprintf(copy, sizeof(copy), "%s", cptr->rooms);
   char *save = NULL;
   bool sent = false;

   for (char *room = strtok_r(copy, ",", &save) ; room ; room = strtok_r(NULL, ",", &save)) {
      for (rrconn_t *recipient = http_client_list ; recipient ; recipient = recipient->next) {
         if (ws_client_in_room(recipient, room)) {
            ws_send_userinfo_room(cptr, recipient, room);
            sent = true;
         }
      }
   }

   if (!sent) {
      ws_send_userinfo_room(cptr, NULL, ws_authoritative_room());
   }

   return true;
}

bool ws_send_room_users(rrconn_t *cptr, const char *room) {
   if (!cptr || !room || !ws_client_in_room(cptr, room)) {
      return false;
   }

   event_emit(RR_SERVER_ROSTER_EVENT, cptr, room);

   for (rrconn_t *current = http_client_list ; current ; current = current->next) {
      if (ws_client_in_room(current, room)) {
         ws_send_userinfo_room(current, cptr, room);
      }
   }

   return true;
}

// Send info on all online users to the user
bool ws_send_users(rrconn_t *cptr) {
   if (cptr) {
      /* A roster is scoped to the rooms the recipient has joined.  Include the room on each userinfo record so clients can keep independent user lists for side
       * chats.
       */
      char copy[AUTOJOIN_LEN];
      snprintf(copy, sizeof(copy), "%s", cptr->rooms);
      char *save = NULL;

      for (char *room = strtok_r(copy, ",", &save) ; room ; room = strtok_r(NULL, ",", &save)) {
         for (rrconn_t *current = http_client_list ; current ; current = current->next) {
            if (ws_client_in_room(current, room)) {
               ws_send_userinfo_room(current, cptr, room);
            }
         }
      }

      return true;
   }

   for (rrconn_t *current = http_client_list ; current ; current = current->next) {
      ws_send_userinfo(current, NULL);
   }

   return true;
}

///////////////////////
// MUTE: Mute a user //
///////////////////////
static bool ws_chat_cmd_mute(rrconn_t *cptr, const char *target, const char *reason) {
   if (!cptr || !cptr->user) {
      return true;
   }

   if (!target) {
      ws_send_error(cptr, "No target given for MUTE");

      return true;
   }

   if (has_priv(cptr->user->uid, "admin|owner")) {
      rrconn_t *acptr = http_find_client_by_name(target);

      if (!acptr) {
         return true;
      }
      acptr->user->is_muted = true;

      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG + 1];
      prepare_msg(msgbuf, sizeof(msgbuf), "%s MUTEd by %s: Reason: %s", target, cptr->chatname, (reason ? reason : "No reason given"));
      send_global_alert("***SERVER***", msgbuf);

      // broadcast the userinfo so cul updates
      ws_send_userinfo(acptr, NULL);

      // Account mute affects every session, including a separately keyed session.
      for (rrconn_t *session = http_client_list ; session ; session = session->next) {
         if (session->user == acptr->user && session->is_ptt) {
            ws_release_ptt_on_disconnect(session);
         }
      }
   } else {
      ws_chat_err_noprivs(cptr, "MUTE");

      return true;
   }

   return false;
}

///////////////////////////
// UNMUTE: Unmute a user //
///////////////////////////
static bool ws_chat_cmd_unmute(rrconn_t *cptr, const char *target) {
   if (!cptr) {
      return true;
   }

   if (!target) {
      ws_send_error(cptr, "No target given for UNMUTE");

      return true;
   }

   if (!cptr->user) {
      return true;
   }

   if (has_priv(cptr->user->uid, "admin|owner")) {
      rrconn_t *acptr = http_find_client_by_name(target);

      if (!acptr) {
         return true;
      }
      acptr->user->is_muted = false;

      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG + 1];
      prepare_msg(msgbuf, sizeof(msgbuf), "%s UNMUTEd by %s", target, cptr->chatname);
      send_global_alert("***SERVER***", msgbuf);
      // broadcast the userinfo so cul updates
      ws_send_userinfo(acptr, NULL);
   } else {
      ws_chat_err_noprivs(cptr, "UNMUTE");

      return true;
   }

   return false;
}

// Toggle syslog
static bool ws_chat_cmd_syslog(rrconn_t *cptr, const char *state) {
   if (!cptr || !state) {
      return true;
   }

   if (cptr->user && has_priv(cptr->user->uid, "admin|owner")) {
      bool new_state = false;

      new_state = parse_bool(state);

      if (new_state) {
         client_set_flag(cptr, FLAG_SYSLOG);
      } else {
         client_clear_flag(cptr, FLAG_SYSLOG);
      }
   } else {
      ws_chat_err_noprivs(cptr, "SYSLOG");

      return true;
   }

   return false;
}

/* The owning application may authorize authenticated remote room chat through destination-side grants. Never infer authority from the origin account. */
static bool (*chat_resource_authorizer)(rrconn_t *, const char *, const char *);
void ws_chat_set_resource_authorizer(bool (*authorize) (rrconn_t *, const char *, const char *)) {
   chat_resource_authorizer = authorize;
}
static bool ws_chat_has_authority(rrconn_t *client, const char *room, const char *command) {
   return has_priv(client->user->uid, "admin|owner|chat") || (chat_resource_authorizer && chat_resource_authorizer(client, room, command));
}

bool ws_handle_chat_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      return false;
   }

   if (!cptr->user) {
      Log(LOG_WARN, "chat", "talk parse, cptr:<%p> ->user NULL", cptr);

      return false;
   }

   cptr->last_heard = now;
   cptr->last_chat = now;

   const char *cmd = dict_get(d, "talk.cmd", NULL);
   const char *data = dict_get(d, "talk.data", NULL);
   const char *target = dict_get(d, "talk.target", NULL);
   const char *reason = dict_get(d, "talk.args.reason", NULL);
   const char *msg_type = dict_get(d, "talk.msg_type", NULL);
   const char *user = cptr->chatname;

   // Use the authoritative rig room unless the client explicitly targets a room.
   const char *channel = ws_authoritative_room();

   if (target) {
      channel = target;
   }

   if (cmd) {
      if (strcasecmp(cmd, "qrz") == 0 || strcasecmp(cmd, "grid") == 0) {
         if (!data || !*data) {
            ws_send_error(cptr, "Usage: /%s VALUE", strcasecmp(cmd, "grid") == 0 ? "grid" : "qrz");

            return false;
         }
         char lookup_data[256];
         snprintf(lookup_data, sizeof(lookup_data), "%s", data);
         bool no_cache = false;

         if (strcasecmp(cmd, "qrz") == 0) {
            char *option = strpbrk(lookup_data, " \t");

            if (option) {
               *option++ = '\0';
               while (*option == ' ' || *option == '\t') {
                  option++;
               }

               if (strcasecmp(option, "nocache") != 0) {
                  ws_send_error(cptr, "Invalid /qrz option: %s", option);

                  return false;
               }
               no_cache = true;
            }

            for (const unsigned char *p = (const unsigned char *)lookup_data ; *p ; p++) {
               if (!isalnum(*p) && *p != '-' && *p != '/' && *p != '.') {
                  ws_send_error(cptr, "Invalid callsign: %s", lookup_data);

                  return false;
               }
            }
         } else {
            for (const unsigned char *p = (const unsigned char *)lookup_data ; *p ; p++) {
               if (!isalnum(*p) && *p != '-' && *p != '.' && *p != ',' && *p != '+' && *p != ' ') {
                  ws_send_error(cptr, "Invalid grid or coordinates: %s", data);

                  return false;
               }
            }
         }
         char *lookup_program = cfg_get_path("callsign-lookup:path");
         bool lookup_configured = lookup_program && *lookup_program && config_file && *config_file;
         free(lookup_program);

         if (!lookup_configured) {
            ws_send_error(cptr, "Callsign lookup is not configured on the server");

            return false;
         }
         /* lookup_data is bounded to 255 bytes above; leave room for the command prefix and optional NOCACHE suffix. */
         char request[512];
         snprintf(request, sizeof(request), "/%s %s", strcasecmp(cmd, "grid") == 0 ? "GRID" : "CALL", lookup_data);

         if (no_cache) {
            strncat(request, " NOCACHE", sizeof(request) - strlen(request) - 1);
         }

         if (!callsign_lookup_request(cptr, request)) {
            ws_send_error(cptr, "Callsign lookup is still starting; please retry shortly");

            return false;
         }

         return true;
      } else if (strcasecmp(cmd, "list") == 0) {
         dict *list = dict_new();
         dict_add(list, "msg.type", "talk");
         dict_add(list, "talk.cmd", "room-list");
         dict_add_ulong(list, "msg.ts", now);
         event_emit_dict("room.list", cptr, list);
         dict_free(list);

         return true;
      } else if (strcasecmp(cmd, "room") == 0) {
         char argbuf[256];
         snprintf(argbuf, sizeof(argbuf), "%s", data ? data : "");
         char *save = NULL;
         char *room_or_list = strtok_r(argbuf, " \t", &save);
         char *action = strtok_r(NULL, " \t", &save);
         const char *usage = "Usage: /room list | add #room | remove #room [-f [-h]] [token] | #room vfo (add|list|remove) [rig0.]vfo_a";

         if (!room_or_list) {
            ws_send_error(cptr, "%s", usage);

            return false;
         }

         if (strcasecmp(room_or_list, "list") == 0) {
            if (action) {
               ws_send_error(cptr, "%s", usage);

               return false;
            }
            dict *list = dict_new();
            dict_add(list, "msg.type", "talk");
            dict_add(list, "talk.cmd", "room-list");
            dict_add_ulong(list, "msg.ts", now);
            event_emit_dict("room.list", cptr, list);
            dict_free(list);

            return true;
         }

         // PARITY: native/browser clients forward room management to this parser.
         if (!strcasecmp(room_or_list, "remove") || !strcasecmp(room_or_list, "add")) {
            char *verb = room_or_list;
            room_or_list = action;
            action = verb;

            if (!room_or_list) {
               ws_send_error(cptr, "%s", usage);

               return false;
            }
         }

         if (!ws_room_name_valid(room_or_list)) {
            ws_send_error(cptr, "Invalid room name: %s (room names must start with # or &)", room_or_list);

            return false;
         }
         const char *room = room_canonical(room_or_list);

         if (!action) {
            ws_send_error(cptr, "%s", usage);

            return false;
         }

         if (strcasecmp(action, "add") == 0 || strcasecmp(action, "remove") == 0) {
            char *confirmation = NULL;
            bool force = false, history = false, invalid = false;
            char *option;
            while ((option = strtok_r(NULL, " \t", &save))) {
               if (!strcmp(option, "--force") || !strcmp(option, "-f")) {
                  force = true;
               } else if (!strcmp(option, "--history") || !strcmp(option, "-h")) {
                  history = true;
               } else if (*option == '-' || confirmation) {
                  invalid = true;
               } else {
                  confirmation = option;
               }
            }

            if (invalid || (history && !force) || (!strcasecmp(action, "add") && (confirmation || force || history)) ||
               ((!strcasecmp(action, "remove") || ws_room_station_scoped(room)) && !has_priv(cptr->user->uid, "admin|owner"))) {
               ws_send_error(cptr, "Usage: /room add #room | remove #room [-f [-h]] [token] (admin or owner required)");

               return false;
            }

            if (strcasecmp(action, "remove") == 0) {
               if (strcasecmp(room, ws_authoritative_room()) == 0 || strcasecmp(room, ws_site_room()) == 0) {
                  ws_send_error(cptr, "The site lobby and default rig room cannot be deleted");

                  return false;
               }
               dict *deleted = dict_new();
               dict_add(deleted, "msg.type", "talk");
               dict_add(deleted, "talk.cmd", "room-removed");
               dict_add(deleted, "talk.room", room);
               dict_add(deleted, "talk.user", cptr->chatname);

               if (confirmation) {
                  dict_add(deleted, "talk.confirmation", confirmation);
               }
               dict_add_bool(deleted, "talk.force", force);
               dict_add_bool(deleted, "talk.history", history);
               dict_add_ulong(deleted, "msg.ts", now);
               event_emit_dict("room.delete", cptr, deleted);
               dict_free(deleted);
            } else {
               if (ws_room_rig_base(room)) {
                  ws_send_error(cptr, "Base rig rooms are created only by server rig initialization");

                  return false;
               }
               dict *added = dict_new();
               dict_add(added, "msg.type", "talk");
               dict_add(added, "talk.cmd", "room-added");
               dict_add(added, "talk.room", room);
               dict_add(added, "talk.user", cptr->chatname);
               dict_add_ulong(added, "msg.ts", now);
               event_emit_dict("room.add", cptr, added);
               dict_free(added);
            }

            return true;
         }

         if (strcasecmp(action, "vfo") == 0) {
            char *vfo_action = strtok_r(NULL, " \t", &save);
            char *binding = strtok_r(NULL, " \t", &save);

            if (!vfo_action || (strcasecmp(vfo_action, "list") != 0 && !binding) || strtok_r(NULL, " \t", &save)) {
               ws_send_error(cptr, "%s", usage);

               return false;
            }

            if (strcasecmp(vfo_action, "list") == 0) {
               dict *list = dict_new();
               dict_add(list, "msg.type", "talk");
               dict_add(list, "talk.cmd", "room-vfo-list");
               dict_add(list, "talk.room", room);
               dict_add_ulong(list, "msg.ts", now);
               event_emit_dict("room.vfo-list", cptr, list);
               dict_free(list);

               return true;
            }

            if ((strcasecmp(vfo_action, "add") != 0 && strcasecmp(vfo_action, "remove") != 0) || !has_priv(cptr->user->uid, "admin|owner")) {
               ws_send_error(cptr, "Usage: /room #room vfo add|remove [rig0.]vfo_a (admin or owner required)");

               return false;
            }

            if (strcasecmp(vfo_action, "add") == 0 && !ws_room_rig_namespace(room)) {
               ws_send_error(cptr, "Only this site's numbered rig rooms may have VFO controls");

               return false;
            }
            char normalized[128];

            if (strncasecmp(binding, "rig", 3) != 0) {
               snprintf(normalized, sizeof(normalized), "rig0.%s", binding);
            } else {
               snprintf(normalized, sizeof(normalized), "%s", binding);
            }
            dict *vm = dict_new();
            dict_add(vm, "msg.type", "talk");
            dict_add(vm, "talk.cmd", "room-vfo");
            dict_add(vm, "talk.action", vfo_action);
            dict_add(vm, "talk.room", room);
            dict_add(vm, "talk.vfo", normalized);
            dict_add(vm, "talk.user", cptr->chatname);
            dict_add_ulong(vm, "msg.ts", now);
            event_emit_dict("room.vfo", cptr, vm);
            dict_free(vm);

            return true;
         }
         ws_send_error(cptr, "%s", usage);

         return false;
      } else if (strcasecmp(cmd, "topic") == 0) {
         const char *requested = target ? target : channel;

         if (!requested || (requested[0] != '#' && requested[0] != '&')) {
            ws_send_error(cptr, "Invalid room name: %s (room names must start with # or &)", requested ? requested : "(none)");

            return false;
         }
         requested = room_canonical(requested);

         if (!ws_client_in_room(cptr, requested) && !has_priv(cptr->user->uid, "admin|owner")) {
            ws_send_error(cptr, "You are not joined to room %s", requested);

            return false;
         }

         if (!data) {
            data = "";
         }

         if (strlen(data) > 512) {
            ws_send_error(cptr, "Topic is too long (maximum 512 characters)");

            return false;
         }

         if (*data && cptr->user->is_muted) {
            ws_send_error(cptr, "You are muted and cannot change room topics");

            return false;
         }

         if (*data && !ws_chat_has_authority(cptr, requested, cmd)) {
            ws_send_error(cptr, "You do not have CHAT privilege.");

            return false;
         }
         dict *topic = dict_new();
         dict_add(topic, "msg.type", "talk");
         dict_add(topic, "talk.cmd", "topic");
         dict_add(topic, "talk.room", requested);
         dict_add(topic, "talk.topic", data);
         dict_add(topic, "talk.user", cptr->chatname);
         dict_add_bool(topic, "talk.query", *data == '\0');
         dict_add_ulong(topic, "msg.ts", now);
         event_emit_dict("room.topic", cptr, topic);
         dict_free(topic);

         return true;
      } else if (strcasecmp(cmd, "join") == 0 || strcasecmp(cmd, "part") == 0) {
         const char *requested = target ? target : data;
         bool joining = strcasecmp(cmd, "join") == 0;

         if (requested && *requested && requested[0] != '#' && requested[0] != '&') {
            ws_send_error(cptr, "Invalid room name: %s (room names must start with # or &)", requested);

            return false;
         }
         bool room_ok = joining ? ws_client_join_room(cptr, requested) : ws_client_part_room(cptr, requested);

         if (!room_ok) {
            ws_send_error(cptr, "%s failed for room %s", joining ? "JOIN" : "PART", requested ? requested : "(none)");

            return false;
         }
         dict *room_msg = dict_new();
         dict_add(room_msg, "msg.type", "talk");
         dict_add(room_msg, "talk.cmd", joining ? "join" : "part");
         dict_add(room_msg, "talk.target", room_canonical(requested));
         dict_add(room_msg, "talk.room", room_canonical(requested));
         dict_add_bool(room_msg, "room.has-vfos", ws_room_has_vfos(requested));
         dict_add_bool(room_msg, "room.tx-control", ws_room_tx_control(requested));
         dict_add_bool(room_msg, "room.rx-tunable", ws_room_rx_tunable(requested));
         dict_add_ulong(room_msg, "room.rx-tuning-mask", ws_room_rx_tuning_mask(requested));
         dict_add_ulong(room_msg, "room.vfo-mask", ws_room_vfo_mask(requested));
         dict_add(room_msg, "talk.user", cptr->chatname);
         dict_add_ulong(room_msg, "msg.ts", now);
         ws_broadcast_room_dict(cptr, room_msg, room_canonical(requested));

         /* Removing the room from cptr->rooms above means the normal room broadcast no longer reaches the departing session.  Send the PART confirmation
          * directly so its client can close the tab. */
         if (!joining) {
            /* The session token is an authentication secret; include it only in the departing client's private confirmation, never in the room broadcast. */
            dict_add(room_msg, "talk.session", cptr->token);
            ws_send_dict(cptr, cptr, room_msg, WEBSOCKET_OP_TEXT);
         }

         if (joining) {
            ws_send_room_users(cptr, room_canonical(requested));
         }

         if (joining) {
            event_emit_dict("room.join", cptr, room_msg);
            media_send_available_all(cptr);
         }
         dict_free(room_msg);

         return true;
      } else if (strcasecmp(cmd, "typing") == 0) {
         /* Ephemeral room presence: never trust sender identity, sequence or lease supplied by the client. The room owner authorizes it. */
         const char *state = dict_get(d, "talk.typing", "");

         if (!target || (*target != '#' && *target != '&') || !ws_room_name_valid(target)) {
            ws_send_error(cptr, "Cannot update typing status: specify a valid chat room (talk.target)");

            return false;
         }

         if (!ws_client_in_room(cptr, target)) {
            ws_send_error(cptr, "Cannot update typing status in %s: join the room first", target);

            return false;
         }

         if (!ws_chat_has_authority(cptr, target, "typing")) {
            ws_send_error(cptr, "Cannot update typing status in %s: chat permission is required", target);

            return false;
         }

         if (strcmp(state, "started") && strcmp(state, "active") && strcmp(state, "paused") && strcmp(state, "idle")) {
            ws_send_error(cptr, "Invalid typing status for %s: expected started, active, paused or idle", target);

            return false;
         }

         if (cptr->user->is_muted && strcmp(state, "idle")) {
            ws_send_error(cptr, "Cannot show typing status in %s: your account is muted", target);

            return false;
         }
         dict *presence = dict_new();

         if (!presence) {
            ws_send_error(cptr, "Cannot update typing status in %s: server is out of memory", target);

            return false;
         }
         dict_add(presence, "msg.type", "talk");
         dict_add(presence, "talk.cmd", "typing");
         dict_add(presence, "talk.target", target);
         dict_add(presence, "talk.room", target);
         dict_add(presence, "talk.typing", state);
         dict_add(presence, "talk.from", cptr->chatname);
         event_emit_dict("talk.typing", cptr, presence);
         dict_free(presence);

         return true;
      } else if (strcasecmp(cmd, "msg") == 0) {
         if (cptr->user->is_muted) {
            ws_send_error(cptr, "You are muted and cannot send messages");

            return false;
         }

         if (!data) {
            Log(LOG_DEBUG, "chat", "got msg for cptr <%p> with no data: chatname: %s", cptr, user);

            return false;
         }

         // If the message is empty, just return success
         if (strlen(data) == 0) {
            Log(LOG_CRAZY, "chat", "talk msg has no data");

            return true;
         }

         if (!ws_chat_has_authority(cptr, channel, cmd)) {
            Log(LOG_CRAZY, "chat", "user %s doesn't have chat privileges but tried to send a message", user);

            // XXX: Alert the user that their message was NOT delivered
            // because they aren't allowed to send it.
            ws_send_error(cptr, "You do not have CHAT privilege.");

            return false;
         }

         if (channel && (channel[0] == '#' || channel[0] == '&') && !ws_client_in_room(cptr, channel)) {
            ws_send_error(cptr, "You are not joined to room %s", channel);

            return false;
         }

         // sanity check
         if (!user) {
            Log(LOG_CRAZY, "chat", "talk parse, msg has no user field");

            return false;
         }

         if (msg_type) {
            if (strcasecmp(msg_type, "file_chunk") == 0 ||
               strcasecmp(msg_type, "pub") == 0 ||
               strcasecmp(msg_type, "action") == 0 || strcasecmp(msg_type, "priv") == 0 || strcasecmp(msg_type, "privmsg") == 0) {

               /*
                * Commands are handled locally and don't become chat messages. The resulting CAT events are handled/relayed separately.
                */
               if (strcasecmp(msg_type, "pub") == 0 || strcasecmp(msg_type, "action") == 0) {

                  if (data[0] == '!') {
                     const char *input = data;
                     char cmd[16], arg[32];
                     size_t cmd_len = sizeof(cmd);
                     size_t arg_len = sizeof(arg);

                     if (!has_priv(cptr->user->uid, "admin|owner|tx|noob") || cptr->user->is_muted) {
                        /// XXX: we should send an error alert
                        return false;
                     }

                     while (*input) {
                        while (isspace(*input) || (*input == '!')) {
                           input++;
                        }
                        // extract command
                        size_t i = 0;

                        while (*input && !isspace(*input) && i < cmd_len - 1) {
                           cmd[i++] = *input++;
                        }
                        cmd[i] = '\0';

                        while (isspace(*input)) {
                           input++;
                        }
                        // extract argument
                        i = 0;

                        while (*input && !isspace(*input) && i < arg_len - 1) {
                           arg[i++] = *input++;
                        }
                        arg[i] = '\0';

                        // Stop only when there's no command left to parse.
                        // Commands that take no argument (e.g. !help) must
                        // still reach the dispatch below.
                        if (*cmd == '\0') {
                           break;
                        }

                        if (!strcasecmp(cmd, "freq") || !strcasecmp(cmd, "mode") || !strcasecmp(cmd, "width") || !strcasecmp(cmd, "power")) {
                           dict *control = dict_new();

                           if (!control) {
                              return false;
                           }
                           dict_add(control, "cat.cmd", cmd);
                           dict_add(control, "cat.room", channel);
                           dict_add(control, "cat.vfo", vfo_name(ws_chat_selected_vfo(cptr, channel)));

                           if (!strcasecmp(cmd, "freq")) {
                              dict_add_long(control, "cat.freq", parse_freq(arg));
                           } else if (!strcasecmp(cmd, "mode")) {
                              dict_add(control, "cat.mode", arg);
                           } else if (!strcasecmp(cmd, "power")) {
                              dict_add(control, "cat.power", arg);
                           } else {
                              dict_add(control, "cat.width", arg);
                           }
                           bool applied = ws_handle_rigctl_msg(cptr, control);
                           dict_free(control);

                           if (!applied) {
                              return false;
                           }
                           continue;
                        }

                        if (strcasecmp(cmd, "help") == 0) {
                           // XXX: These should move to help/ and get served
                           // via that mechanism.
                           // Plain text, one notice per line: notices are
                           // sent unescaped so both the C client and the
                           // webui can display them as-is. (PARITY:
                           // rustyrig-www/js/webui notice rendering)
                           ws_send_notice(cptr, "***SERVER***");
                           ws_send_notice(cptr, "*** !help for VFO commands ***");
                           ws_send_notice(cptr, "  !freq <freq> - Set frequency to <freq> - can be 7200 7.2m 7200000 etc form");
                           ws_send_notice(cptr, "  !mode <mode> - Set mode to CW|AM|LSB|USB|FM|DL|DU");
                           ws_send_notice(cptr, "  !power <power> - Set power in watts (e.g. !power 25)");
                           ws_send_notice(cptr, "  !vfo <vfo> - Switch VFOs (A|B|C)");
                           ws_send_notice(cptr, "  !width <width> - Set passband width (narrow|normal|wide)");

                           return true;

                        } else if (strcasecmp(cmd, "vfo") == 0) {
                           if (!ws_client_in_room(cptr, channel) || !ws_room_has_vfos(channel)) {
                              ws_send_error(cptr, "Select a VFO from a joined rig room");

                              return false;
                           }

                           if (*arg == '\0') {
                              ws_send_error(cptr, "!vfo requires a vfo argument (A|B|C...)");

                              return false;
                           }

                           Log(LOG_DEBUG, "ws.chat", "Got !vfo %s from %s", arg, cptr->chatname);

                           rr_vfo_t new_vfo = vfo_lookup(toupper(arg[0]));

                           if (new_vfo < 0 || new_vfo >= MAX_VFOS) {
                              ws_send_error(cptr, "Unknown VFO: !vfo %s (try A-Z)", arg);

                              return false;
                           }

                           if (!(ws_room_vfo_mask(channel) & (UINT32_C(1) << new_vfo))) {
                              ws_send_error(cptr, "VFO %s is not mapped to room %s", vfo_name(new_vfo), channel);

                              return false;
                           }

                           if (cptr->is_ptt) {
                              ws_send_error(cptr, "Cannot select VFO %s in room %s while transmitting on VFO %c in room %s; release PTT "
                                 "first", vfo_name(new_vfo), channel, cptr->ptt_vfo, cptr->ptt_room);

                              return false;
                           }

                           cptr->chat_vfo = (char)('A' + new_vfo);
                           strlcpy(cptr->chat_vfo_room, channel, sizeof(cptr->chat_vfo_room));
                           /* Room-scoped hardware selection, not just the UI session VFO. */
                           dict *request = dict_new();

                           if (request && ws_room_control_allowed(cptr, channel, false) && ws_supervision_allowed(cptr, channel, false)) {
                              dict_add(request, "rig.room", channel);
                              dict_add(request, "rig.vfo", vfo_name(new_vfo));
                              event_emit_dict("be.vfo.select", cptr, request);
                           }

                           if (request) {
                              dict_free(request);
                           }
                           dict *selected = dict_new();
                           dict_add(selected, "msg.type", "cat");
                           dict_add(selected, "cat.room", channel);
                           dict_add(selected, "cat.state.vfo", vfo_name(new_vfo));
                           // Session selection is distinct from the physical radio active VFO.
                           dict_add_bool(selected, "cat.state.selected", true);
                           dict_add_bool(selected, "cat.state.active", true); // older clients
                           ws_send_dict(NULL, cptr, selected, WEBSOCKET_OP_TEXT);
                           dict_free(selected);

                        } else {
                           Log(LOG_WARN, "ws.chat", "Unknown command: %s", cmd);
                           ws_send_error(cptr, "Unknown command: !%s - try !help", cmd);

                           return false;
                        }
                     }
                     // These events shouldn't get relayed because the CAT
                     // events generated above will be relayed separately.
                     return true;
                  }
               }

               /*
                * Normal chat message, or file chunk.
                *
                * The protocol layer creates the semantic event. The rrserver event handler is responsible for broadcasting, logging, persistence, and other
                * server-side actions.
                */
               bool private_msg = strcasecmp(msg_type, "priv") == 0 || strcasecmp(msg_type, "privmsg") == 0;

               if (private_msg && (!channel || (channel[0] == '#' || channel[0] == '&'))) {
                  ws_send_error(cptr, "Private message target must be a username");

                  return false;
               }

               bool global_msg = false;

               if (channel && channel[0] != '&') {
                  // Send the message to all connected servers.
                  global_msg = true;
               }

               dict *talk_msg = dict_new();

               dict_add(talk_msg, "msg.type", "talk");
               dict_add(talk_msg, "talk.cmd", "msg");
               dict_add(talk_msg, "talk.data", data);
               dict_add(talk_msg, "talk.from", cptr->chatname);
               dict_add(talk_msg, "talk.target", channel);
               dict_add(talk_msg, "talk.msg_type", msg_type);
               dict_add_bool(talk_msg, "talk.msg.global", global_msg);
               dict_add_ulong(talk_msg, "msg.ts", now);

               if (private_msg) {
                  dict_add(talk_msg, "talk.msg_type", "priv");
               }

               /*
                * File chunks need their additional metadata preserved.
                */
               if (strcasecmp(msg_type, "file_chunk") == 0) {
                  dict_add(talk_msg, "talk.msg_id", dict_get(d, "talk.msg_id", ""));
                  const char *filetype = dict_get(d, "talk.filetype", NULL);
                  const char *filename = dict_get(d, "talk.filename", NULL);

                  long chunk_index = dict_get_long(d, "talk.chunk_index", 0);

                  long total_chunks = dict_get_long(d, "talk.total_chunks", 0);

                  dict_add_double(talk_msg, "talk.chunk_index", chunk_index);

                  dict_add_double(talk_msg, "talk.total_chunks", total_chunks);

                  dict_add(talk_msg, "talk.filename", filename);

                  dict_add(talk_msg, "talk.filetype", filetype);
               }

               Log(LOG_CRAZY, "ws.chat", "Emitting talk.msg event: from=<%s> target=<%s> type=<%s> data=<%s>", cptr->chatname, channel, msg_type, data);

               event_emit_dict("talk.msg", cptr, talk_msg);

               Log(LOG_CRAZY, "ws.chat", "Returned from talk.msg event");

               dict_free(talk_msg);

               return true;

            } else {
               Log(LOG_DEBUG, "ws.chat", "unknown message type: %s", msg_type);
            }
         }
      } else if (strcasecmp(cmd, "whois") == 0) {
         if (!target) {
            Log(LOG_DEBUG, "chat", "whois with no target");
            ws_send_error(cptr, "No target given for WHOIS");

            return false;
         }

         rrconn_t *acptr = http_find_client_by_name(target);

         if (!acptr || !acptr->user) {
            ws_send_error(cptr, "WHOIS: no such user: %s", target);

            return false;
         }

         // Public metadata allowlist: never copy account credentials, tokens or nonces.
         // Flat whois reply, keyed off talk.<field> - shared by webui and
         // rrclient (chat.whois.c). Sessions is just the count (also in userinfo).
         dict *wi = dict_new();
         char *rooms = ws_chat_whois_rooms(acptr);

         if (!wi || !rooms) {
            dict_free(wi);
            free(rooms);
            ws_send_error(cptr, "WHOIS: unable to collect room membership");

            return false;
         }
         dict_add(wi, "talk.rooms", rooms);
         free(rooms);

         dict_add(wi, "msg.type", "talk");
         dict_add(wi, "talk.cmd", "whois");
         dict_add_ulong(wi, "msg.ts", now);

         dict_add(wi, "talk.username", acptr->user->name);
         dict_add(wi, "talk.email", acptr->user->email);
         dict_add(wi, "talk.privs", acptr->user->privs);
         dict_add_bool(wi, "talk.muted", acptr->user->is_muted);
         dict_add_int(wi, "talk.sessions", acptr->user->sessions);

         // Session info of the first matching connection
         dict_add_ulong(wi, "talk.connected", (unsigned long)acptr->session_start);
         dict_add_ulong(wi, "talk.last_heard", (unsigned long)acptr->last_heard);
         dict_add(wi, "talk.ua", acptr->user_agent ? acptr->user_agent : "unknown");

         event_emit_dict("protocol.whois", cptr, wi);
         dict_free(wi);
      } else if (strcasecmp(cmd, "die") == 0) {
         ws_chat_cmd_die(cptr, reason);

      } else if (strcasecmp(cmd, "quota") == 0) {
         // PARITY: rustyrig-www/js/webui.chat.js /quota (sends talk.cmd=quota)
         // Admin/owner only; the server program does the quota work via the
         // quota.cmd event (sqlite lives in rrserver, not in the library)
         if (!has_priv(cptr->user->uid, "admin|owner")) {
            ws_chat_err_noprivs(cptr, "QUOTA");

            return false;
         }

         // data is the command tail: LIST | SHOW <user>... | ADD <mins> <user>...
         // | RESET <user>... | SET <mins> <user>...; target carries a single
         // user name for the SHOW case (webui passes it via talk.target)
         dict *q = dict_new();

         if (!q) {
            Log(LOG_WARN, "chat", "quota cmd: failed to create dict");

            return false;
         }
         dict_add(q, "msg.type", "quota.cmd");
         dict_add(q, "quota.from", cptr->chatname);
         dict_add(q, "quota.data", data ? data : "");

         if (target) {
            dict_add(q, "quota.target", target);
         }
         event_emit_dict("quota.cmd", cptr, q);
         dict_free(q);

      } else if (!strcasecmp(cmd, "supervise")) {
         event_emit_dict("supervise.cmd", cptr, d);
      } else if (!strcasecmp(cmd, "link") || !strcasecmp(cmd, "admin")) {
         if (!has_priv(cptr->user->uid, "admin|owner")) {
            ws_chat_err_noprivs(cptr, cmd);

            return false;
         }
         /* Applications own stored peers and database administration. */
         event_emit_dict(!strcasecmp(cmd, "link") ? "link.cmd" : "admin.cmd", cptr, d);

      } else if (strcasecmp(cmd, "user") == 0) {
         /* User administration is implemented by the server because the protocol library must not know how accounts are persisted. */
         if (!has_priv(cptr->user->uid, "admin|owner") && (!data || strncasecmp(data, "pass ", 5))) {
            ws_chat_err_noprivs(cptr, "USER");

            return false;
         }
         dict *u = dict_new();

         if (!u) {
            ws_send_error(cptr, "USER: out of memory");

            return false;
         }
         dict_add(u, "msg.type", "user.cmd");
         dict_add(u, "user.from", cptr->chatname);
         dict_add(u, "user.data", data ? data : "");
         /* Optional account profile for the server-owned user creation path. The server validates fields and persists them with bound SQL. */
         const char *profile_keys[] = {
            "email", "real_name", "phone", "maxsessions"
         };

         for (size_t profile_index = 0 ; profile_index < sizeof(profile_keys) / sizeof(profile_keys[0]) ; profile_index++) {
            char key[48];
            snprintf(key, sizeof(key), "user.%s", profile_keys[profile_index]);
            const char *value = dict_get(d, key, NULL);

            if (value) {
               dict_add(u, key, value);
            }
         }

         event_emit_dict("user.cmd", cptr, u);
         dict_free(u);

      } else if (strcasecmp(cmd, "kick") == 0) {
         ws_chat_cmd_kick(cptr, target, reason);

      } else if (strcasecmp(cmd, "mute") == 0) {
         ws_chat_cmd_mute(cptr, target, reason);

      } else if (strcasecmp(cmd, "names") == 0) {
         ws_send_users(cptr);

      } else if (strcasecmp(cmd, "restart") == 0) {
         ws_chat_cmd_restart(cptr, reason);

      } else if (strcasecmp(cmd, "syslog") == 0) {
         ws_chat_cmd_syslog(cptr, target);

      } else if (strcasecmp(cmd, "media") == 0) {
         // Media channel admin: /media LIST | SUBSCRIBE <uuid|#> |
         // UNSUBSCRIBE <uuid|#> | REMOVE <uuid|#>. LIST/SUBSCRIBE/UNSUBSCRIBE
         // are available to any authenticated user; REMOVE needs admin|owner.
         // PARITY: rustyrig-www/js/webui.media.js (channel management)
         char sub[32] = "LIST";
         char marg[64] = "";

         if (data && data[0] != '\0') {
            size_t i = 0;

            while (*data && !isspace((unsigned char)*data) && i < sizeof(sub) - 1) {
               sub[i++] = *data++;
            }
            sub[i] = '\0';
            while (isspace((unsigned char)*data)) {
               data++;
            }
            strlcpy(marg, data, sizeof(marg));
         }

         if (strcasecmp(sub, "REMOVE") == 0 && !has_priv(cptr->user->uid, "admin|owner")) {
            ws_chat_err_noprivs(cptr, "MEDIA REMOVE");

            return false;
         }
         dict *m = dict_new();

         if (!m) {
            return false;
         }

         if (strcasecmp(sub, "REMOVE") == 0) {
            // Server program (rrserver) owns removal: it notifies all clients
            // via media.chan-remove and clears the registry entry.
            dict_add(m, "media.chan-uuid", marg);
            event_emit_dict("remove-media-channel", cptr, m);
            dict_free(m);

            return true;
         }
         dict_add(m, "msg.type", "media");
         dict_add(m, "media.cmd", strcasecmp(sub, "UNSUBSCRIBE") == 0 ? "unsubscribe" : (strcasecmp(sub, "SUBSCRIBE") == 0 ? "subscribe" : "list"));
         dict_add_ulong(m, "media.ts", now);

         if (marg[0] != '\0') {
            dict_add(m, "media.chan-uuid", marg);
         }
         ws_handle_mediachan_msg(cptr, m);
         dict_free(m);

      } else if (strcasecmp(cmd, "unmute") == 0) {
         ws_chat_cmd_unmute(cptr, target);
      } else {
         Log(LOG_WARN, "chat", "Unknown command: %s", cmd);
         ws_send_error(cptr, "Invalid command: %s", cmd);

         return false;
      }
   }

   return true;
}

/* Save/load the component view when entering a server connection context. */
typedef struct {
   ws_room_meta_t room_meta[32];
   uint32_t authoritative_vfo_mask;
   char authoritative_room_override[128];
} ws_rooms_context;
void ws_rooms_context_swap(void **saved) {
   if (!*saved) {
      *saved = calloc(1, sizeof(ws_rooms_context));

      if (!*saved) {
         abort();
      }
      ((ws_rooms_context *) *saved)->authoritative_vfo_mask = 3;
   }
   ws_rooms_context *state = *saved;
   {
      __typeof__(room_meta) temporary;
      memcpy(&temporary, &room_meta, sizeof(room_meta));
      memcpy(&room_meta, &state->room_meta, sizeof(room_meta));
      memcpy(&state->room_meta, &temporary, sizeof(room_meta));
   }
   {
      __typeof__(authoritative_vfo_mask) temporary;
      memcpy(&temporary, &authoritative_vfo_mask, sizeof(authoritative_vfo_mask));
      memcpy(&authoritative_vfo_mask, &state->authoritative_vfo_mask, sizeof(authoritative_vfo_mask));
      memcpy(&state->authoritative_vfo_mask, &temporary, sizeof(authoritative_vfo_mask));
   }
   {
      __typeof__(authoritative_room_override) temporary;
      memcpy(&temporary, &authoritative_room_override, sizeof(authoritative_room_override));
      memcpy(&authoritative_room_override, &state->authoritative_room_override, sizeof(authoritative_room_override));
      memcpy(&state->authoritative_room_override, &temporary, sizeof(authoritative_room_override));
   }
}
