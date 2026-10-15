//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// RustyRig directory metadata service: no receiver probes or implicit streams.
#include <glib.h>
#include <sqlite3.h>
#include <errno.h>
#include <limits.h>
#include <glib/gstdio.h>
#include <curl/curl.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include "receiver.directory.h"

#define DIRECTORY_MAX_BODY (8 * 1024 * 1024)
#define DIRECTORY_MAX_ROWS 10000
#define DIRECTORY_INTERVAL 3600
extern time_t now;
static GPtrArray *receivers;
static char *payload, *source, *etag, *modified;
static time_t next_request;
static char status_text[512] = "No directory loaded. Import a list or configure a permitted mirror.";
static bool initialized;
static sqlite3 *cache_db;
static guint completion_source, learn_source;
static GHashTable *pending_learn;
struct directory_job {
   char *url, *etag, *modified;
   GString *body;
   GPtrArray *parsed;
   char *response_etag, *response_modified;
   long status;
   time_t started;
   unsigned delay;
   CURLcode result;
   gint done, cancel;
   GThread *thread;
};
static struct directory_job *job;

static void changed(void) {
   event_emit("client.receiver-directory.changed", NULL, NULL);
}
static void receiver_free(gpointer value) {
   receiver_directory_entry_t *r = value;
   g_free(r->url); g_free(r->name); g_free(r->location); g_free(r->contact);
   g_free(r->antenna); g_free(r->hardware); g_free(r->grid); g_free(r->properties);
   g_free(r->local_state); g_free(r->local_reason);
   g_free(r);
}
static char *field(struct mg_str object, const char *key) {
   char *value = mg_json_get_str(object, key);
   char *valid = g_utf8_make_valid(value ? value : "", -1);
   free(value);
   if (g_utf8_strlen(valid, -1) > 512) {
      *g_utf8_offset_to_pointer(valid, 512) = 0;
   }
   for (char *p = valid; *p; p++) {
      if ((unsigned char)*p < 32 || *p == 127) *p = ' ';
   }
   return valid;
}
static long long integer(struct mg_str object, const char *key, long long fallback) {
   struct mg_str token = mg_json_get_tok(object, key);
   if (!token.len || token.len > 20) return fallback;
   char text[24];
   memcpy(text, token.buf, token.len); text[token.len] = 0;
   char *end;
   errno = 0;
   long long number = strtoll(text, &end, 10);
   return errno || end == text || *end ? fallback : number;
}
static bool flag(struct mg_str object, const char *key) {
   bool value;
   if (mg_json_get_bool(object, key, &value)) return value;
   struct mg_str token = mg_json_get_tok(object, key);
   if (!token.len) return false;
   char *text = mg_json_get_str(object, key);
   bool result = !text || (strcasecmp(text, "false") && strcasecmp(text, "no") && strcmp(text, "0"));
   free(text);
   return result;
}
static GUri *http_uri(const char *url) {
   if (!url || !*url || strlen(url) > 2048) return NULL;
   for (const char *p = url; *p; p++) {
      if ((unsigned char)*p <= 32 || *p == 127) return NULL;
   }
   GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
   if (uri && (!g_uri_get_host(uri) || !*g_uri_get_host(uri) || g_uri_get_userinfo(uri) ||
      (g_ascii_strcasecmp(g_uri_get_scheme(uri), "http") && g_ascii_strcasecmp(g_uri_get_scheme(uri), "https")))) {
      g_uri_unref(uri); uri = NULL;
   }
   return uri;
}
static char *canonical_url(const char *url) {
   GUri *uri = http_uri(url);
   if (!uri) return NULL;
   char *scheme = g_ascii_strdown(g_uri_get_scheme(uri), -1), *host = g_ascii_strdown(g_uri_get_host(uri), -1);
   const char *path = g_uri_get_path(uri);
   int port = g_uri_get_port(uri);
   if ((port == 80 && !strcmp(scheme, "http")) || (port == 443 && !strcmp(scheme, "https"))) port = -1;
   char *result = g_uri_join(G_URI_FLAGS_NONE, scheme, NULL, host, port, path && strcmp(path, "/") ? path : "", g_uri_get_query(uri), g_uri_get_fragment(uri));
   g_free(scheme); g_free(host); g_uri_unref(uri);
   return result;
}
static bool directory_url_allowed(const char *url) {
   GUri *uri = http_uri(url);
   if (!uri) return false;
   const char *host = g_uri_get_host(uri);
   char *lower = g_ascii_strdown(host, -1);
   bool loopback = !strcmp(lower, "localhost") || !strcmp(lower, "127.0.0.1") || !strcmp(lower, "::1");
   bool ok = (loopback || !g_ascii_strcasecmp(g_uri_get_scheme(uri), "https")) &&
      strcmp(lower, "kiwisdr.com") && !g_str_has_suffix(lower, ".kiwisdr.com");
   g_free(lower); g_uri_unref(uri);
   return ok;
}

static GPtrArray *parse_kiwi(const char *data, size_t length) {
   if (!data || !length || length > DIRECTORY_MAX_BODY || memchr(data, 0, length)) return NULL;
   struct mg_str json = mg_str_n(data, length);
   int size = 0, start = mg_json_get(json, "$", &size);
   if (start < 0 || size < 2 || data[start] != '{') return NULL;
   for (size_t i = start + size; i < length; i++) if (!g_ascii_isspace(data[i])) return NULL;
   if (integer(json, "$.schema", -1) != 1) return NULL;
   /* The first provider is the documented Kiwi schema-1 mirror. Other
    * providers must have explicit parsers, never inherit Kiwi API policy. */
   char *provider = mg_json_get_str(json, "$.provider");
   bool supported = provider ? !strcmp(provider, "kiwi") : !mg_json_get_tok(json, "$.provider").len;
   free(provider);
   if (!supported) return NULL;
   struct mg_str array = mg_json_get_tok(json, "$.receivers"), object;
   if (!array.len || array.buf[0] != '[') return NULL;
   GPtrArray *list = g_ptr_array_new_with_free_func(receiver_free);
   size_t offset = 0, count = 0;
   while ((offset = mg_json_next(array, offset, NULL, &object))) {
      if (++count > DIRECTORY_MAX_ROWS || !object.len || object.buf[0] != '{') goto invalid;
      receiver_directory_entry_t *r = g_new0(receiver_directory_entry_t, 1);
      r->driver = "kiwi";
      r->url = field(object, "$.url");
      GUri *uri = http_uri(r->url);
      if (!uri) { g_free(r->url); g_free(r); continue; }
      g_uri_unref(uri);
      char *normalized = canonical_url(r->url);
      g_free(r->url); r->url = normalized;
      r->name = field(object, "$.name"); r->location = field(object, "$.loc");
      r->contact = field(object, "$.contact");
      if (!*r->contact) { g_free(r->contact); r->contact = field(object, "$.email"); }
      r->antenna = field(object, "$.antenna"); r->hardware = field(object, "$.sdr_hw");
      r->grid = field(object, "$.grid");
      r->users = CLAMP(integer(object, "$.users", 0), 0, INT_MAX);
      r->users_max = CLAMP(integer(object, "$.users_max", 0), 0, INT_MAX);
      r->ext_api = CLAMP(integer(object, "$.ext_api", -1), -1, INT_MAX);
      r->offline = flag(object, "$.offline"); r->flagged = flag(object, "$.flagged");
      long long low = integer(object, "$.bands[0]", -1), high = integer(object, "$.bands[1]", -1);
      if (low >= 0 && high > low) { r->low_hz = low; r->high_hz = high; }
      r->properties = g_utf8_make_valid(object.buf, object.len);
      g_ptr_array_add(list, r);
   }
   return list;
invalid:
   g_ptr_array_free(list, TRUE);
   return NULL;
}
/* Provider dispatch is intentionally separate from transport/cache/UI. Add
 * other WebSDR adapters here with their own schema and operator-policy rules. */
static GPtrArray *parse(const char *data, size_t length) {
   return parse_kiwi(data, length);
}
bool receiver_directory_entry_accessible(const receiver_directory_entry_t *r) {
   return r && r->driver && !strcmp(r->driver, "kiwi") && r->ext_api > 0 && !r->flagged && !r->offline && !r->deleted &&
      (!r->local_state || !strcmp(r->local_state, "unknown"));
}
char *receiver_directory_bridge_config(const receiver_directory_entry_t *r) {
   if (!receiver_directory_entry_accessible(r) || strcmp(r->driver, "kiwi")) return NULL;
   GUri *uri = http_uri(r->url);
   /* The current bridge driver supports plaintext root-host Kiwi endpoints. */
   if (!uri) return NULL;
   const char *host = g_uri_get_host(uri), *path = g_uri_get_path(uri);
   for (const char *p = host; *p; p++) {
      if (!g_ascii_isalnum(*p) && *p != '.' && *p != '-' && *p != '_') {
         g_uri_unref(uri); return NULL;
      }
   }
   if (g_ascii_strcasecmp(g_uri_get_scheme(uri), "http") || strchr(host, ':') ||
      (path && *path && strcmp(path, "/")) || g_uri_get_query(uri) || g_uri_get_fragment(uri)) {
      g_uri_unref(uri); return NULL;
   }
   int port = g_uri_get_port(uri);
   char *coverage = r->high_hz ? g_strdup_printf("rx-min-hz = %" G_GUINT64_FORMAT "\nrx-max-hz = %" G_GUINT64_FORMAT "\n", r->low_hz, r->high_hz) : g_strdup("");
   char *config = g_strdup_printf("; Directory hints only; add receiver-selected to [bridge] instances.\n"
      "; Operator API allowance: %d channels. Limits may apply.\n"
      "[bridge:receiver-selected]\nenabled = true\ndriver = kiwi\nhost = %s\nport = %d\nrig = receiver-selected\n"
      "%srx-modes = USB LSB AM CW\nrx-audio = true\ntx-supported = false\n", r->ext_api, host, port < 0 ? 80 : port, coverage);
   g_free(coverage); g_uri_unref(uri);
   return config;
}

static char *cache_path(void) {
   const char *configured = cfg_get("receiver-directory.cache");
   return configured && *configured ? g_strdup(configured) :
      g_build_filename(g_get_user_cache_dir(), "rustyrig", "receivers.sqlite3", NULL);
}
static void cache_write(void) {
   if (!cache_db) return;
   sqlite3_stmt *statement = NULL;
   if (sqlite3_prepare_v2(cache_db, "INSERT OR REPLACE INTO directory_cache VALUES(1,?,?,?,?,?)", -1, &statement, NULL) == SQLITE_OK) {
      sqlite3_bind_text(statement, 1, source ? source : "", -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(statement, 2, payload ? payload : "", -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(statement, 3, etag ? etag : "", -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(statement, 4, modified ? modified : "", -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(statement, 5, next_request);
      if (sqlite3_step(statement) != SQLITE_DONE) Log(LOG_WARN, "receiver.directory", "Cannot save directory cache: %s", sqlite3_errmsg(cache_db));
   }
   sqlite3_finalize(statement);
}
static const char *detail_keys[] = { "name", "location", "contact", "antenna", "hardware", "grid", "low_hz", "high_hz", "ext_api" };
static void detail_set(const char *url, const char *key, const char *value, bool live, time_t stamp) {
   if (!cache_db || !value || !*value) return;
   sqlite3_stmt *statement = NULL;
   const char *sql = "INSERT INTO receiver_details VALUES(?,?,?,?,?) ON CONFLICT(url,key) DO UPDATE SET "
      "value=excluded.value,origin=excluded.origin,updated_at=excluded.updated_at "
      "WHERE (excluded.origin='live' AND (receiver_details.origin!='live' OR excluded.updated_at>=receiver_details.updated_at)) OR (excluded.origin='directory' AND receiver_details.origin!='live')";
   if (sqlite3_prepare_v2(cache_db, sql, -1, &statement, NULL) == SQLITE_OK) {
      sqlite3_bind_text(statement, 1, url, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(statement, 2, key, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(statement, 3, value, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(statement, 4, live ? "live" : "directory", -1, SQLITE_STATIC);
      sqlite3_bind_int64(statement, 5, stamp);
      sqlite3_step(statement);
   }
   sqlite3_finalize(statement);
}
static void apply_details(receiver_directory_entry_t *r) {
   sqlite3_stmt *statement = NULL;
   if (sqlite3_prepare_v2(cache_db, "SELECT key,value,origin,updated_at FROM receiver_details WHERE url=?", -1, &statement, NULL) != SQLITE_OK) return;
   sqlite3_bind_text(statement, 1, r->url, -1, SQLITE_TRANSIENT);
   while (sqlite3_step(statement) == SQLITE_ROW) {
      const char *key = (const char *)sqlite3_column_text(statement, 0);
      const char *value = (const char *)sqlite3_column_text(statement, 1);
      char **field = !strcmp(key, "name") ? &r->name : !strcmp(key, "location") ? &r->location : !strcmp(key, "contact") ? &r->contact :
         !strcmp(key, "antenna") ? &r->antenna : !strcmp(key, "hardware") ? &r->hardware : !strcmp(key, "grid") ? &r->grid : NULL;
      if (field) { g_free(*field); *field = g_strdup(value); }
      else if (!strcmp(key, "low_hz")) r->low_hz = g_ascii_strtoull(value, NULL, 10);
      else if (!strcmp(key, "high_hz")) r->high_hz = g_ascii_strtoull(value, NULL, 10);
      else if (!strcmp(key, "ext_api")) r->ext_api = CLAMP(g_ascii_strtoll(value, NULL, 10), -1, INT_MAX);
      if (!strcmp((const char *)sqlite3_column_text(statement, 2), "live")) r->learned_at = MAX(r->learned_at, sqlite3_column_int64(statement, 3));
   }
   sqlite3_finalize(statement);
}
static void reload_records(void) {
   if (!cache_db) return;
   sqlite3_stmt *statement = NULL;
   if (sqlite3_prepare_v2(cache_db, "SELECT record,last_seen,deleted,offline,provider FROM receiver_records ORDER BY deleted,last_seen DESC,url LIMIT 10000", -1, &statement, NULL) != SQLITE_OK) return;
   GPtrArray *list = g_ptr_array_new_with_free_func(receiver_free);
   while (sqlite3_step(statement) == SQLITE_ROW) {
      const char *record = (const char *)sqlite3_column_text(statement, 0);
      char *wrapped = g_strdup_printf("{\"schema\":1,\"provider\":\"%s\",\"receivers\":[%s]}", (const char *)sqlite3_column_text(statement, 4), record);
      GPtrArray *entry = parse(wrapped, strlen(wrapped));
      if (entry && entry->len == 1) {
         receiver_directory_entry_t *r = g_ptr_array_steal_index(entry, 0);
         r->last_seen = sqlite3_column_int64(statement, 1);
         r->deleted = sqlite3_column_int(statement, 2);
         r->offline = sqlite3_column_int(statement, 3);
         apply_details(r);
         g_ptr_array_add(list, r);
      }
      if (entry) g_ptr_array_free(entry, TRUE);
      g_free(wrapped);
   }
   sqlite3_finalize(statement);
   g_ptr_array_free(receivers, TRUE); receivers = list;
}
static void merge_records(void) {
   if (!cache_db) return;
   if (sqlite3_exec(cache_db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
      sqlite3_exec(cache_db, "ROLLBACK", NULL, NULL, NULL);
      Log(LOG_WARN, "receiver.directory", "Cannot merge directory cache: %s", sqlite3_errmsg(cache_db));
      return;
   }
   sqlite3_stmt *statement = NULL;
   char *provider = payload ? field(mg_str(payload), "$.provider") : g_strdup("");
   if (!*provider) { g_free(provider); provider = g_strdup("kiwi"); }
   if (sqlite3_prepare_v2(cache_db, "UPDATE receiver_records SET deleted=1 WHERE provider=?", -1, &statement, NULL) == SQLITE_OK) {
      sqlite3_bind_text(statement, 1, provider, -1, SQLITE_TRANSIENT); sqlite3_step(statement);
   }
   sqlite3_finalize(statement); statement = NULL; g_free(provider);
   sqlite3_prepare_v2(cache_db, "INSERT INTO receiver_records(url,record,last_seen,deleted,offline,provider) VALUES(?,?,?,0,?,?) ON CONFLICT(url) DO UPDATE SET record=excluded.record,last_seen=MAX(receiver_records.last_seen,excluded.last_seen),deleted=0,offline=excluded.offline,provider=excluded.provider", -1, &statement, NULL);
   bool ok = statement != NULL;
   for (guint i = 0; ok && i < receivers->len; i++) {
      receiver_directory_entry_t *r = g_ptr_array_index(receivers, i);
      sqlite3_bind_text(statement, 1, r->url, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(statement, 2, r->properties, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(statement, 3, now);
      sqlite3_bind_int(statement, 4, r->offline);
      sqlite3_bind_text(statement, 5, r->driver, -1, SQLITE_TRANSIENT);
      ok = sqlite3_step(statement) == SQLITE_DONE; sqlite3_reset(statement); sqlite3_clear_bindings(statement);
      const char *values[] = { r->name, r->location, r->contact, r->antenna, r->hardware, r->grid };
      for (unsigned key = 0; key < G_N_ELEMENTS(values); key++) detail_set(r->url, detail_keys[key], values[key], false, now);
      if (r->high_hz) {
         char low[32], high[32];
         g_snprintf(low, sizeof(low), "%" G_GUINT64_FORMAT, r->low_hz); g_snprintf(high, sizeof(high), "%" G_GUINT64_FORMAT, r->high_hz);
         detail_set(r->url, "low_hz", low, false, now); detail_set(r->url, "high_hz", high, false, now);
      }
   }
   sqlite3_finalize(statement);
   if (!ok) {
      sqlite3_exec(cache_db, "ROLLBACK", NULL, NULL, NULL);
      Log(LOG_WARN, "receiver.directory", "Directory cache merge failed; preserving previous records");
      return;
   }
   // Keep historical offline/missing entries until absent for ninety days.
   char *purge = sqlite3_mprintf("DELETE FROM receiver_details WHERE url IN(SELECT url FROM receiver_records WHERE deleted=1 AND last_seen<%lld);"
      "DELETE FROM receiver_state WHERE url IN(SELECT url FROM receiver_records WHERE deleted=1 AND last_seen<%lld);"
      "DELETE FROM receiver_records WHERE deleted=1 AND last_seen<%lld;COMMIT;", (long long)now - 90*86400, (long long)now - 90*86400, (long long)now - 90*86400);
   sqlite3_exec(cache_db, purge, NULL, NULL, NULL); sqlite3_free(purge);
   reload_records();
}
static void restore_states(void) {
   if (!cache_db) return;
   sqlite3_stmt *statement = NULL;
   if (sqlite3_prepare_v2(cache_db, "SELECT state,reason,updated_at FROM receiver_state WHERE url=?", -1, &statement, NULL) != SQLITE_OK) return;
   for (guint i = 0; i < receivers->len; i++) {
      receiver_directory_entry_t *r = g_ptr_array_index(receivers, i);
      g_clear_pointer(&r->local_state, g_free); g_clear_pointer(&r->local_reason, g_free);
      r->state_updated = 0;
      sqlite3_bind_text(statement, 1, r->url, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(statement) == SQLITE_ROW) {
         r->local_state = g_strdup((const char *)sqlite3_column_text(statement, 0));
         r->local_reason = g_strdup((const char *)sqlite3_column_text(statement, 1));
         r->state_updated = sqlite3_column_int64(statement, 2);
      }
      sqlite3_reset(statement); sqlite3_clear_bindings(statement);
   }
   sqlite3_finalize(statement);
}
static bool read_bounded(const char *path, size_t limit, char **data, size_t *length) {
   FILE *file = fopen(path, "rb");
   if (!file) return false;
   GString *body = g_string_new(NULL);
   char buffer[8192];
   size_t bytes;
   bool ok = true;
   while ((bytes = fread(buffer, 1, sizeof(buffer), file))) {
      if (body->len + bytes > limit) { ok = false; break; }
      g_string_append_len(body, buffer, bytes);
   }
   ok &= !ferror(file);
   fclose(file);
   *length = body->len; *data = g_string_free(body, !ok);
   return ok;
}
static void ensure(void) {
   if (initialized) return;
   initialized = true;
   receivers = g_ptr_array_new_with_free_func(receiver_free);
   char *path = cache_path(), *parent = g_path_get_dirname(path);
   if (!g_mkdir_with_parents(parent, 0700) && sqlite3_open(path, &cache_db) == SQLITE_OK) {
      g_chmod(path, 0600);
      sqlite3_busy_timeout(cache_db, 100);
      sqlite3_limit(cache_db, SQLITE_LIMIT_LENGTH, DIRECTORY_MAX_BODY + 8192);
      if (sqlite3_exec(cache_db,
         "CREATE TABLE IF NOT EXISTS directory_cache(id INTEGER PRIMARY KEY CHECK(id=1),source TEXT,payload TEXT,etag TEXT,modified TEXT,next_request INTEGER);"
         "CREATE TABLE IF NOT EXISTS receiver_records(url TEXT PRIMARY KEY,record TEXT NOT NULL,last_seen INTEGER NOT NULL,deleted INTEGER NOT NULL DEFAULT 0,offline INTEGER NOT NULL DEFAULT 0,provider TEXT NOT NULL DEFAULT 'kiwi');"
         "CREATE TABLE IF NOT EXISTS receiver_details(url TEXT,key TEXT,value TEXT NOT NULL,origin TEXT NOT NULL,updated_at INTEGER NOT NULL,PRIMARY KEY(url,key));"
         "CREATE TABLE IF NOT EXISTS receiver_state(url TEXT PRIMARY KEY,state TEXT NOT NULL CHECK(state IN ('unknown','unreachable','restricted')),reason TEXT NOT NULL,updated_at INTEGER NOT NULL);",
         NULL, NULL, NULL) != SQLITE_OK) {
         sqlite3_close(cache_db); cache_db = NULL;
      }
   } else if (cache_db) { sqlite3_close(cache_db); cache_db = NULL; }
   if (cache_db) {
      sqlite3_exec(cache_db, "ALTER TABLE receiver_records ADD COLUMN offline INTEGER NOT NULL DEFAULT 0", NULL, NULL, NULL);
      sqlite3_exec(cache_db, "ALTER TABLE receiver_records ADD COLUMN provider TEXT NOT NULL DEFAULT 'kiwi'", NULL, NULL, NULL);
   }
   sqlite3_stmt *statement = NULL;
   if (cache_db && sqlite3_prepare_v2(cache_db, "SELECT source,payload,etag,modified,next_request FROM directory_cache WHERE id=1", -1, &statement, NULL) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW) {
      source = g_strdup((const char *)sqlite3_column_text(statement, 0));
      payload = g_strdup((const char *)sqlite3_column_text(statement, 1));
      etag = g_strdup((const char *)sqlite3_column_text(statement, 2));
      modified = g_strdup((const char *)sqlite3_column_text(statement, 3));
      next_request = sqlite3_column_int64(statement, 4);
      GPtrArray *list = payload ? parse(payload, strlen(payload)) : NULL;
      if (list) {
         g_ptr_array_free(receivers, TRUE); receivers = list;
         g_snprintf(status_text, sizeof(status_text), "Cached directory: %u receivers. Published availability may be stale.", receivers->len);
      } else {
         g_clear_pointer(&payload, g_free); g_clear_pointer(&etag, g_free); g_clear_pointer(&modified, g_free);
      }
   }
   sqlite3_finalize(statement);
   reload_records();
   restore_states();
   g_free(path); g_free(parent);
}
bool receiver_directory_learn(const char *endpoint, dict *fields) {
   ensure();
   char *url = canonical_url(endpoint);
   const char *driver = fields ? dict_get(fields, "driver", "kiwi") : "kiwi";
   if (strcmp(driver, "kiwi")) { g_free(url); return false; } // Needs its own provider adapter.
   GUri *uri = http_uri(url);
   if (!cache_db || !uri || !fields) { if (uri) g_uri_unref(uri); g_free(url); return false; }
   g_uri_unref(uri);
   time_t stamp = (time_t)dict_get_llong(fields, "connected_at", now);
   if (stamp <= 0 || stamp > now) stamp = now;
   sqlite3_exec(cache_db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
   sqlite3_stmt *statement = NULL;
   char *escaped = json_escape(url), *record = g_strdup_printf("{\"url\":\"%s\"}", escaped);
   free(escaped);
   if (sqlite3_prepare_v2(cache_db, "INSERT INTO receiver_records(url,record,last_seen,deleted,offline) VALUES(?,?,?,1,0) ON CONFLICT(url) DO UPDATE SET last_seen=MAX(receiver_records.last_seen,excluded.last_seen)", -1, &statement, NULL) == SQLITE_OK) {
      sqlite3_bind_text(statement, 1, url, -1, SQLITE_TRANSIENT); sqlite3_bind_text(statement, 2, record, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(statement, 3, stamp); sqlite3_step(statement);
   }
   sqlite3_finalize(statement); g_free(record);
   for (unsigned i = 0; i < G_N_ELEMENTS(detail_keys); i++) {
      const char *value = dict_get(fields, detail_keys[i], NULL);
      if (value && strlen(value) <= 512) detail_set(url, detail_keys[i], value, true, stamp);
   }
   sqlite3_exec(cache_db, "COMMIT", NULL, NULL, NULL);
   reload_records(); restore_states(); changed();
   g_free(url);
   return true;
}
static gboolean flush_learn(gpointer unused) {
   (void)unused;
   learn_source = 0;
   GHashTable *pending = pending_learn;
   pending_learn = NULL;
   GHashTableIter iter;
   gpointer url, fields;
   g_hash_table_iter_init(&iter, pending);
   while (g_hash_table_iter_next(&iter, &url, &fields)) receiver_directory_learn(url, fields);
   g_hash_table_destroy(pending);
   return G_SOURCE_REMOVE;
}
void receiver_directory_queue_learn(const char *url, dict *fields) {
   if (!url || !*url || !fields) return;
   if (!pending_learn) pending_learn = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)dict_free);
   dict *copy = g_hash_table_lookup(pending_learn, url);
   if (!copy) { copy = dict_new(); g_hash_table_insert(pending_learn, g_strdup(url), copy); }
   dict_merge(copy, fields);
   if (!learn_source) learn_source = g_idle_add(flush_learn, NULL);
}
bool receiver_directory_set_state(const char *endpoint, const char *state, const char *reason) {
   ensure();
   if (!cache_db || !endpoint || !state || (strcmp(state, "unknown") && strcmp(state, "unreachable") && strcmp(state, "restricted"))) return false;
   char *url = canonical_url(endpoint);
   if (!url) return false;
   sqlite3_stmt *statement = NULL;
   bool ok = sqlite3_prepare_v2(cache_db, "INSERT OR REPLACE INTO receiver_state VALUES(?,?,?,?)", -1, &statement, NULL) == SQLITE_OK;
   if (ok) {
      sqlite3_bind_text(statement, 1, url, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(statement, 2, state, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(statement, 3, reason ? reason : "User report", -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(statement, 4, now);
      ok = sqlite3_step(statement) == SQLITE_DONE;
   }
   sqlite3_finalize(statement);
   if (ok) { restore_states(); changed(); }
   g_free(url);
   return ok;
}
size_t receiver_directory_count(void) { ensure(); return receivers->len; }
const receiver_directory_entry_t *receiver_directory_receiver(size_t index) {
   ensure(); return index < receivers->len ? g_ptr_array_index(receivers, index) : NULL;
}
const char *receiver_directory_status(void) { ensure(); return status_text; }
bool receiver_directory_busy(void) { return job != NULL; }
char *receiver_directory_info(void) {
   ensure();
   char *published = payload ? field(mg_str(payload), "$.fetched_at") : g_strdup("");
   GDateTime *date = *published ? g_date_time_new_from_iso8601(published, NULL) : NULL;
   char *age = date ? g_strdup_printf("%lld minutes old", (long long)MAX(0, (now - g_date_time_to_unix(date)) / 60)) : g_strdup("age unknown");
   char *info = g_strdup_printf("Refresh source: %s\nDirectory published: %s (%s)", source && *source ? source : "Local import / unconfigured", *published ? published : "Not supplied", age);
   if (date) g_date_time_unref(date);
   g_free(published); g_free(age);
   return info;
}

static size_t receive_body(char *data, size_t size, size_t count, void *user) {
   struct directory_job *j = user;
   if (size && count > DIRECTORY_MAX_BODY / size) return 0;
   size_t bytes = size * count;
   if (j->body->len + bytes > DIRECTORY_MAX_BODY) return 0;
   g_string_append_len(j->body, data, bytes);
   return bytes;
}
static size_t receive_header(char *data, size_t size, size_t count, void *user) {
   struct directory_job *j = user;
   size_t bytes = size * count;
   if (bytes > 8192) return 0;
   char *line = g_strndup(data, bytes);
   g_strchomp(line);
   if (g_str_has_prefix(line, "HTTP/")) {
      g_clear_pointer(&j->response_etag, g_free); g_clear_pointer(&j->response_modified, g_free);
      j->delay = DIRECTORY_INTERVAL;
   } else if (!g_ascii_strncasecmp(line, "ETag:", 5) && strlen(line + 5) < 512) {
      g_free(j->response_etag); j->response_etag = g_strdup(g_strstrip(line + 5));
   } else if (!g_ascii_strncasecmp(line, "Last-Modified:", 14) && strlen(line + 14) < 128) {
      g_free(j->response_modified); j->response_modified = g_strdup(g_strstrip(line + 14));
   } else if (!g_ascii_strncasecmp(line, "Cache-Control:", 14)) {
      char *lower = g_ascii_strdown(line, -1), *age = strstr(lower, "max-age=");
      if (age) j->delay = MAX(j->delay, MIN(g_ascii_strtoull(age + 8, NULL, 10), 604800));
      g_free(lower);
   } else if (!g_ascii_strncasecmp(line, "Retry-After:", 12)) {
      char *value = g_strstrip(line + 12), *end;
      unsigned long seconds = strtoul(value, &end, 10);
      if (*end) { time_t date = curl_getdate(value, NULL); seconds = date > j->started ? date - j->started : 0; }
      j->delay = MAX(j->delay, MIN(seconds, 604800));
   }
   g_free(line);
   return bytes;
}
static int cancel_fetch(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
   (void)a; (void)b; (void)c; (void)d;
   return g_atomic_int_get(&((struct directory_job *)user)->cancel);
}
static bool safe_validator(const char *value) {
   if (!value || strlen(value) > 512) return false;
   for (const char *p = value; *p; p++) if ((unsigned char)*p < 32 || *p == 127) return false;
   return true;
}
static gpointer fetch(gpointer user) {
   struct directory_job *j = user;
   CURL *curl = curl_easy_init();
   j->result = CURLE_FAILED_INIT;
   if (curl) {
      struct curl_slist *headers = NULL;
      if (safe_validator(j->etag) && *j->etag) {
         char *h = g_strdup_printf("If-None-Match: %s", j->etag);
         headers = curl_slist_append(headers, h); g_free(h);
      } else if (safe_validator(j->modified) && *j->modified) {
         char *h = g_strdup_printf("If-Modified-Since: %s", j->modified);
         headers = curl_slist_append(headers, h); g_free(h);
      }
      curl_easy_setopt(curl, CURLOPT_URL, j->url);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "RustyRig/" VERSION " ReceiverDirectory");
#if LIBCURL_VERSION_NUM >= 0x075500
      curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https,http");
#else
      curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS | CURLPROTO_HTTP);
#endif
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
      curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_body);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, j);
      curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, receive_header);
      curl_easy_setopt(curl, CURLOPT_HEADERDATA, j);
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, cancel_fetch);
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, j);
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
      j->result = curl_easy_perform(curl);
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &j->status);
      curl_slist_free_all(headers); curl_easy_cleanup(curl);
   }
   if (j->result == CURLE_OK && j->status == 200) j->parsed = parse(j->body->str, j->body->len);
   g_atomic_int_set(&j->done, 1);
   return NULL;
}
static void job_free(void) {
   g_thread_join(job->thread);
   g_free(job->url); g_free(job->etag); g_free(job->modified);
   g_free(job->response_etag); g_free(job->response_modified);
   if (job->parsed) g_ptr_array_free(job->parsed, TRUE);
   g_string_free(job->body, TRUE); g_free(job); job = NULL;
}
static gboolean finish(gpointer unused) {
   (void)unused;
   if (!g_atomic_int_get(&job->done)) return G_SOURCE_CONTINUE;
   GPtrArray *list = job->parsed;
   job->parsed = NULL;
   bool unchanged = job->result == CURLE_OK && job->status == 304 && payload;
   if (list || unchanged) {
      if (list) {
         g_ptr_array_free(receivers, TRUE); receivers = list;
         g_free(payload); payload = g_strndup(job->body->str, job->body->len);
         g_free(etag); etag = g_strdup(job->response_etag);
         g_free(modified); modified = g_strdup(job->response_modified);
      }
      struct mg_str json = mg_str(payload);
      char *published = field(json, "$.fetched_at");
      g_snprintf(status_text, sizeof(status_text), "%u receivers; directory published %s. Availability is a cached hint.", receivers->len, *published ? published : "at an unknown time");
      g_free(published);
   } else {
      g_snprintf(status_text, sizeof(status_text), "Directory refresh failed (%s, HTTP %ld). Keeping %u cached receivers; retry later.",
         job->result == CURLE_OK && job->status == 200 ? "invalid or unsupported JSON" : curl_easy_strerror(job->result), job->status, receivers->len);
   }
   next_request = now + job->delay;
   if (unchanged) {
      GPtrArray *fresh = parse(payload, strlen(payload));
      if (fresh) { g_ptr_array_free(receivers, TRUE); receivers = fresh; merge_records(); }
   } else if (list) merge_records();
   restore_states();
   cache_write();
   job_free(); completion_source = 0;
   changed();
   return G_SOURCE_REMOVE;
}
bool receiver_directory_refresh(void) {
   ensure();
   if (job) return false;
   const char *url = g_getenv("RRCLIENT_RECEIVER_DIRECTORY_URL");
   if (!url || !*url) url = cfg_get("receiver-directory.url");
   if (!url || !*url) {
      g_strlcpy(status_text, "No mirror configured. Import JSON or set receiver-directory.url to a permitted mirror.", sizeof(status_text));
      changed(); return false;
   }
   if (!directory_url_allowed(url)) {
      g_strlcpy(status_text, "Use an HTTPS mirror, not the KiwiSDR directory origin. No request sent.", sizeof(status_text));
      changed(); return false;
   }
   if (!g_strcmp0(source, url) && now < next_request) {
      g_snprintf(status_text, sizeof(status_text), "Using %u cached receivers. Next refresh allowed in %lld seconds.", receivers->len, (long long)(next_request - now));
      changed(); return false;
   }
   static gsize curl_ready;
   if (g_once_init_enter(&curl_ready)) {
      CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
      g_once_init_leave(&curl_ready, result == CURLE_OK ? 1 : 2);
   }
   if (curl_ready != 1) return false;
   job = g_new0(struct directory_job, 1);
   job->started = now;
   job->url = g_strdup(url); job->body = g_string_new(NULL); job->delay = DIRECTORY_INTERVAL;
   if (!g_strcmp0(source, url) && payload) {
      job->etag = g_strdup(etag); job->modified = g_strdup(modified);
   }
   if (g_strcmp0(source, url)) {
      g_clear_pointer(&payload, g_free); g_clear_pointer(&etag, g_free); g_clear_pointer(&modified, g_free);
   }
   g_free(source); source = g_strdup(url);
   next_request = now + DIRECTORY_INTERVAL;
   cache_write(); // Persist the attempt so a restart cannot hammer the mirror.
   job->thread = g_thread_new("receiver-directory", fetch, job);
   completion_source = g_timeout_add(50, finish, NULL);
   g_strlcpy(status_text, "Fetching directory metadata from the configured mirror…", sizeof(status_text));
   changed(); return true;
}
bool receiver_directory_import(const char *path) {
   ensure();
   if (job) return false;
   char *data = NULL;
   size_t length;
   GPtrArray *list = read_bounded(path, DIRECTORY_MAX_BODY, &data, &length) ? parse(data, length) : NULL;
   if (!list) {
      g_free(data);
      g_strlcpy(status_text, "Import failed: expected a bounded schema-1 Kiwi directory JSON object. Cached list retained.", sizeof(status_text));
      changed(); return false;
   }
   g_ptr_array_free(receivers, TRUE); receivers = list;
   g_free(payload); payload = data;
   g_clear_pointer(&etag, g_free); g_clear_pointer(&modified, g_free);
   merge_records();
   restore_states();
   cache_write();
   g_snprintf(status_text, sizeof(status_text), "Imported %u receivers. No receiver connections were made.", receivers->len);
   changed(); return true;
}
void receiver_directory_shutdown(void) {
   if (learn_source) { g_source_remove(learn_source); learn_source = 0; }
   if (pending_learn) flush_learn(NULL);
   if (completion_source) { g_source_remove(completion_source); completion_source = 0; }
   if (job) { g_atomic_int_set(&job->cancel, 1); job_free(); }
   if (receivers) g_ptr_array_free(receivers, TRUE);
   receivers = NULL;
   g_clear_pointer(&payload, g_free); g_clear_pointer(&source, g_free);
   g_clear_pointer(&etag, g_free); g_clear_pointer(&modified, g_free);
   if (cache_db) { sqlite3_close(cache_db); cache_db = NULL; }
   next_request = 0; initialized = false;
}
