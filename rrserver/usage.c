#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/traffic.h>
#include <rrserver/usage.h>
#include <rrserver/database.h>
extern time_t now;

bool rr_usage_parse_bytes(const char *text, uint64_t *bytes) {
   if (!text || !*text || !bytes || *text < '0' || *text > '9') return false;
   uint64_t value = 0;
   const char *p = text;
   while (*p >= '0' && *p <= '9') {
      unsigned digit = *p++ - '0';
      if (value > (INT64_MAX - digit) / 10) return false;
      value = value * 10 + digit;
   }
   uint64_t scale;
   switch (*p) {
      case 0: case 'm': case 'M': scale = 1000000; break;
      case 'g': case 'G': scale = 1000000000; break;
      case 't': case 'T': scale = UINT64_C(1000000000000); break;
      case 'p': case 'P': scale = UINT64_C(1000000000000000); break;
      default: return false;
   }
   if (*p && p[1]) return false;
   if (value > INT64_MAX / scale) return false;
   *bytes = value * scale;
   return true;
}

/* Match only simple username globs; patterns never become SQL. */
bool rrserver_quota_matches(const char *pattern, const char *name) {
   if (!pattern || !*pattern || !name || strlen(pattern) > HTTP_USER_LEN) return false;
   for (const unsigned char *p=(const unsigned char *)pattern; *p; p++)
      if (!isalnum(*p) && *p!='_' && *p!='-' && *p!='*' && *p!='?') return false;
   const char *star=NULL, *retry=NULL;
   while (*name) {
      if (*pattern=='?' || (*pattern && tolower((unsigned char)*pattern)==tolower((unsigned char)*name))) { pattern++; name++; }
      else if (*pattern=='*') { star=pattern++; retry=name; }
      else if (star) { pattern=star+1; name=++retry; }
      else return false;
   }
   while (*pattern=='*') pattern++;
   return !*pattern;
}

#ifdef USE_SQLITE
static uint64_t add_saturated(uint64_t a, uint64_t b) {
   return a > INT64_MAX - b ? INT64_MAX : a + b;
}
static uint64_t total_bytes(const struct rr_traffic *traffic) {
   return add_saturated(add_saturated(traffic->tx_text_bytes, traffic->tx_binary_bytes),
      add_saturated(traffic->rx_text_bytes, traffic->rx_binary_bytes));
}
static bool usage_flush(rrconn_t *peer) {
   if (!peer || !peer->user || !peer->authenticated || !masterdb) return false;
   struct rr_traffic delta;
#define DELTA(field) delta.field = peer->traffic.field - peer->usage_checkpoint.field
   DELTA(tx_text_bytes); DELTA(tx_text_frames); DELTA(tx_binary_bytes); DELTA(tx_binary_frames);
   DELTA(rx_text_bytes); DELTA(rx_text_frames); DELTA(rx_binary_bytes); DELTA(rx_binary_frames);
#undef DELTA
   uint64_t at = mono_us();
   uint64_t seconds = peer->traffic_started_us && at >= peer->traffic_started_us ? (at - peer->traffic_started_us) / 1000000 : 0;
   uint64_t elapsed = seconds >= peer->usage_seconds_checkpoint ? seconds - peer->usage_seconds_checkpoint : 0;
   if (total_bytes(&delta) || elapsed) {
      if (!db_usage_record(masterdb, peer->user->name, &delta, elapsed)) {
         Log(LOG_WARN, "usage", "Failed saving session usage for %s; checkpoint retained for retry", peer->user->name);
         return false;
      }
      peer->usage_checkpoint = peer->traffic;
      peer->usage_seconds_checkpoint = seconds;
   }
   struct rr_usage usage;
   if (db_usage_get(masterdb, peer->user->name, &usage) && usage.bandwidth_limited && usage.bandwidth_remaining <= 0 && !peer->bandwidth_warned) {
      peer->bandwidth_warned = true;
      ws_send_notice(peer, "Bandwidth allowance exhausted; contact a station admin about contributing time or topping up your allowance.");
   }
   return true;
}

bool rrserver_usage_flush_user(const char *name) {
   bool saved = true;
   for (rrconn_t *peer = http_client_list; peer; peer = peer->next)
      if (peer->user && name && !strcasecmp(peer->user->name, name) && !usage_flush(peer)) saved = false;
   return saved;
}

static void usage_closed(const char *event, const char *data, rrconn_t *peer, void *user) {
   (void)event; (void)data; (void)user;
   if (!peer || !peer->authenticated || !peer->user) return;
   bool saved = usage_flush(peer);
   const struct rr_traffic *t = &peer->traffic;
   uint64_t at = mono_us(), duration = peer->traffic_started_us && at >= peer->traffic_started_us ? (at - peer->traffic_started_us) / 1000000 : 0;
   Log(LOG_AUDIT, "session.usage", "user=%s session=%lld duration=%" PRIu64 "s tx_time=%" PRIu64 "s tx_text_bytes=%" PRIu64 " tx_text_frames=%" PRIu64
      " tx_binary_bytes=%" PRIu64 " tx_binary_frames=%" PRIu64 " rx_text_bytes=%" PRIu64 " rx_text_frames=%" PRIu64 " rx_binary_bytes=%" PRIu64 " rx_binary_frames=%" PRIu64 " saved=%s",
      peer->user->name, (long long)peer->session_start, duration, peer->session_tx_seconds,
      t->tx_text_bytes, t->tx_text_frames, t->tx_binary_bytes, t->tx_binary_frames,
      t->rx_text_bytes, t->rx_text_frames, t->rx_binary_bytes, t->rx_binary_frames, saved ? "yes" : "no");
}

static void usage_poll(const char *event, const char *data, rrconn_t *peer, void *user) {
   (void)event; (void)data; (void)peer; (void)user;
   static uint64_t checkpoint_at;
   uint64_t at = mono_us();
   if (checkpoint_at && at >= checkpoint_at && at - checkpoint_at < 60000000) return;
   checkpoint_at = at;
   for (rrconn_t *current = http_client_list; current; current = current->next) usage_flush(current);
}

static void usage_add_number(dict *out, const char *key, uint64_t value) {
   char text[32]; snprintf(text, sizeof(text), "%" PRIu64, value); dict_add(out, key, text);
}

static void usage_whois(const char *event, const char *data, rrconn_t *requester, void *user) {
   (void)event; (void)user;
   dict *reply = data ? json2dict(data) : NULL;
   if (!reply || !requester || !requester->authenticated) { dict_free(reply); return; }
   if (requester->user && has_priv(requester->user->uid, "admin|owner|elmer")) {
      const char *name = dict_get(reply, "talk.username", NULL);
      rrserver_usage_flush_user(name);
      struct rr_usage usage;
      if (db_usage_get(masterdb, name, &usage)) {
         rr_traffic_to_dict(reply, "talk.usage", &usage.traffic);
         usage_add_number(reply, "talk.usage.total-bytes", total_bytes(&usage.traffic));
         usage_add_number(reply, "talk.usage.session-seconds", usage.session_seconds);
         usage_add_number(reply, "talk.usage.tx-seconds", usage.tx_seconds);
         usage_add_number(reply, "talk.usage.reset-at", usage.reset_at);
         char remaining[32];
         snprintf(remaining, sizeof(remaining), "%d", db_quota_get(masterdb, name));
         dict_add(reply, "talk.usage.tx-remaining", remaining);
         dict_add_bool(reply, "talk.usage.tx-enforced", cfg_get_bool("quota.enforce", true));
         dict_add(reply, "talk.usage.bandwidth-status", !usage.bandwidth_limited ? "unlimited" : usage.bandwidth_remaining <= 0 ? "exhausted" : "available");
         char allowance[32];
         if (usage.bandwidth_limited) snprintf(allowance, sizeof(allowance), "%" PRId64, usage.bandwidth_remaining);
         else snprintf(allowance, sizeof(allowance), "unlimited");
         dict_add(reply, "talk.usage.bandwidth-remaining", allowance);
      }
   }
   ws_send_dict(NULL, requester, reply, WEBSOCKET_OP_TEXT);
   dict_free(reply);
}

static const char *usage_user(const char *name) {
   if (!name) return NULL;
   for (unsigned i = 0; i < HTTP_MAX_USERS; i++)
      if (http_users[i].name[0] && !strcasecmp(http_users[i].name, name)) return http_users[i].name;
   return NULL;
}

static void usage_show(rrconn_t *requester, const char *name) {
   rrserver_usage_flush_user(name);
   struct rr_usage usage;
   if (!db_usage_get(masterdb, name, &usage)) { ws_send_error(requester, "Cannot read bandwidth usage for %s", name); return; }
   ws_send_notice(requester, "%s: TX remaining=%d seconds (%s)", name, db_quota_get(masterdb, name), cfg_get_bool("quota.enforce", true) ? "enforced" : "advisory");
   if (usage.bandwidth_limited)
      ws_send_notice(requester, "%s: BW used=%" PRIu64 " bytes, remaining=%" PRId64 " bytes; TX used=%" PRIu64 "s, session time=%" PRIu64 "s",
         name, total_bytes(&usage.traffic), usage.bandwidth_remaining, usage.tx_seconds, usage.session_seconds);
   else ws_send_notice(requester, "%s: BW used=%" PRIu64 " bytes, allowance=unlimited; TX used=%" PRIu64 "s, session time=%" PRIu64 "s",
      name, total_bytes(&usage.traffic), usage.tx_seconds, usage.session_seconds);
}

void rrserver_usage_quota(rrconn_t *requester, const char *command, int argc, char **argv) {
   if (!requester || !requester->authenticated || !requester->user || !has_priv(requester->user->uid, "admin|owner")) return;
   if (!strcasecmp(command, "LIST")) {
      for (unsigned i = 0; i < HTTP_MAX_USERS; i++) if (http_users[i].name[0]) usage_show(requester, http_users[i].name);
      return;
   }
   bool reset = !strcasecmp(command, "RESET"), show = !strcasecmp(command, "SHOW");
   bool set = !strcasecmp(command, "SET"), add = !strcasecmp(command, "ADD");
   if ((!reset && !show && !set && !add) || argc < (reset || show ? 1 : 2) || ((set || add) && argc % 2)) {
      ws_send_notice(requester, "Usage: /quota BW LIST | SHOW <user>... | RESET <user>... | SET <user> <amount> | ADD <user> <amount>");
      ws_send_notice(requester, "Amounts: whole 1M chunks; 10=10M, suffixes M/G/T/P use decimal SI bytes. SET 0 means no remaining allowance.");
      return;
   }
   for (int i=0; i<argc; i += reset || show ? 1 : 2) {
      const char *name = usage_user(argv[i]);
      if (!name) { ws_send_error(requester, "quota BW: no such user: %s", argv[i]); continue; }
      if (show) { usage_show(requester, name); continue; }
      uint64_t bytes;
      const char *amount = reset ? cfg_get("quota.bandwidth.default") : argv[i+1];
      if (!amount) amount = "1G";
      if (!rr_usage_parse_bytes(amount, &bytes)) { ws_send_error(requester, "quota BW: invalid amount %s; use whole M/G/T/P units", amount); continue; }
      if (!rrserver_usage_flush_user(name)) {
         ws_send_error(requester, "quota BW %s failed for %s: pending usage could not be saved", command, name);
         continue;
      }
      bool ok = reset ? db_usage_reset(masterdb, name, bytes) : db_usage_bandwidth_set(masterdb, name, bytes, add);
      if (!ok) { ws_send_error(requester, "quota BW %s failed for %s (database error or overflow)", command, name); continue; }
      for (rrconn_t *peer=http_client_list; peer; peer=peer->next)
         if (peer->user && !strcasecmp(peer->user->name,name)) peer->bandwidth_warned = false;
      Log(LOG_AUDIT, "quota.bandwidth", "%s %s %s amount=%" PRIu64 " bytes", requester->user->name, command, name, bytes);
      usage_show(requester, name);
   }
}
#else
static void usage_whois(const char *event, const char *data, rrconn_t *requester, void *user) {
   (void)event; (void)user;
   dict *reply = data ? json2dict(data) : NULL;
   if (reply && requester && requester->authenticated) ws_send_dict(NULL, requester, reply, WEBSOCKET_OP_TEXT);
   dict_free(reply);
}
bool rrserver_usage_flush_user(const char *name) { (void)name; return true; }
void rrserver_usage_quota(rrconn_t *requester, const char *command, int argc, char **argv) { (void)requester; (void)command; (void)argc; (void)argv; }
#endif

void rrserver_usage_register_events(void) {
   event_on("protocol.whois", usage_whois, NULL);
#ifdef USE_SQLITE
   event_on("protocol.session.closed", usage_closed, NULL);
   event_on("server.poll", usage_poll, NULL);
#endif
}
