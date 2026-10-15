// Server configuration and persistence only; drivers retain hardware authority.
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/bridge.config.h>
#include <librrprotocol/vfo.h>
#include <rrserver/bridge.hints.h>
#include <rrserver/database.h>
#include <rrserver/rig.properties.h>

static const char *keys[] = {"rx-min-hz", "rx-max-hz", "rx-modes", "rx-audio", "rx-iq", "rx-waterfall",
   "tx-supported", "tx-min-hz", "tx-max-hz", "tx-modes", "tx-audio"};
static const char *setting(const char *name, const char *key) {
   const char *value = rrp_bridge_config_get(name, key);
   if (value && *value) return value;
   char global[96]; snprintf(global, sizeof(global), "bridge.%s", key);
   return cfg_get(global);
}
static char *identity(const char *name, const char *driver) {
   const char *host = setting(name, "host"), *port = setting(name, "port");
   if (!host) host = "";
   if (!port || !*port) port = "8073";
   GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
   if (!sum) return NULL;
   const char *parts[] = {"bridge-hints-v1", name, driver, host, port};
   for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) g_checksum_update(sum, (const guchar *)parts[i], strlen(parts[i]) + 1);
   char *key = g_strdup(g_checksum_get_string(sum));
   g_checksum_free(sum);
   return key;
}
static bool canonical_modes(const char *modes) {
   gchar **tokens = g_strsplit(modes, " ", -1);
   bool ok = true;
   for (unsigned i = 0; tokens[i]; i++) {
      if (!*tokens[i]) continue;
      rr_mode_t mode = vfo_parse_mode(tokens[i]);
      if (mode == MODE_NONE || strcmp(tokens[i], vfo_mode_name(mode))) ok = false;
   }
   g_strfreev(tokens);
   return ok;
}
static bool decode(dict *data, const char *name, const char *driver, rrp_bridge_ipc_message_t *out) {
   if (!dict_get(data, "rx-min-hz", NULL) || !dict_get(data, "rx-max-hz", NULL) || !dict_get(data, "rx-modes", NULL)) return false;
   rrp_bridge_ipc_init(out, RRP_BRIDGE_IPC_CAPABILITIES);
   snprintf(out->name, sizeof(out->name), "%s", name);
   snprintf(out->driver, sizeof(out->driver), "%s", driver);
   out->flags = RRP_BRIDGE_RX;
   uint64_t *numbers[] = {&out->rx_min_hz, &out->rx_max_hz, &out->tx_min_hz, &out->tx_max_hz};
   const char *number_keys[] = {"rx-min-hz", "rx-max-hz", "tx-min-hz", "tx-max-hz"};
   for (unsigned i = 0; i < 4; i++) {
      const char *value = dict_get(data, number_keys[i], "0");
      if (!*value || *value == '-') return false;
      errno = 0; char *end;
      unsigned long long n = strtoull(value, &end, 10);
      if (errno || *end || end == value) return false;
      *numbers[i] = n;
   }
   const char *rx = dict_get(data, "rx-modes", ""), *tx = dict_get(data, "tx-modes", "");
   if (strlen(rx) >= sizeof(out->rx_modes) || strlen(tx) >= sizeof(out->tx_modes)) return false;
   strcpy(out->rx_modes, rx); strcpy(out->tx_modes, tx);
   const char *bits[] = {"rx-audio", "rx-iq", "rx-waterfall", "tx-supported", "tx-audio"};
   const unsigned flags[] = {RRP_BRIDGE_RX_AUDIO, RRP_BRIDGE_RX_IQ, RRP_BRIDGE_WATERFALL, RRP_BRIDGE_TX, RRP_BRIDGE_TX_AUDIO};
   for (unsigned i = 0; i < 5; i++) {
      const char *value = dict_get(data, bits[i], "false");
      if (!strcasecmp(value, "true") || !strcmp(value, "1")) out->flags |= flags[i];
      else if (strcasecmp(value, "false") && strcmp(value, "0")) return false;
   }
   return out->rx_max_hz <= LONG_MAX && canonical_modes(rx) && canonical_modes(tx) && rrp_bridge_ipc_valid(out);
}
int rrserver_bridge_hints_load(const char *name, const char *driver, rrp_bridge_ipc_message_t *out) {
   if (!name || !driver || !out) return -1;
   dict *data = NULL;
#if defined(USE_SQLITE)
   char *key = identity(name, driver);
   char *json = key ? db_bridge_hints_get(masterdb, key) : NULL;
   g_free(key);
   if (json) { data = json2dict(json); free(json); }
#endif
   int source = 0;
   if (data) {
      if (!strcmp(dict_get(data, "schema", ""), "1") && decode(data, name, driver, out)) source = 1;
      else { Log(LOG_WARN, "bridge", "[%s] ignoring invalid capability cache", name); dict_free(data); data = NULL; }
   }
   if (!data) data = dict_new();
   if (!data) return -1;
   for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
      const char *value = setting(name, keys[i]);
      if (value && *value) {
         if (dict_add(data, keys[i], value)) { dict_free(data); return -1; }
         source = 2;
      }
   }
   bool valid = !source || decode(data, name, driver, out);
   dict_free(data);
   if (!valid) Log(LOG_CRIT, "bridge", "[%s] invalid capability hints: require valid RX coverage, canonical modes and boolean capabilities", name);
   return valid ? source : -1;
}
bool rrserver_bridge_hints_save(const char *name, const char *driver, const rrp_bridge_ipc_message_t *caps) {
#if defined(USE_SQLITE)
   if (!name || !driver || !caps || caps->type != RRP_BRIDGE_IPC_CAPABILITIES || !rrp_bridge_ipc_valid(caps) ||
       strcmp(caps->name, name) || strcmp(caps->driver, driver)) return false;
   dict *data = dict_new();
   char *key = identity(name, driver);
   if (!data || !key) { dict_free(data); g_free(key); return false; }
   dict_add(data, "schema", "1");
   const uint64_t numbers[] = {caps->rx_min_hz, caps->rx_max_hz, caps->tx_min_hz, caps->tx_max_hz};
   const char *number_keys[] = {"rx-min-hz", "rx-max-hz", "tx-min-hz", "tx-max-hz"};
   for (unsigned i = 0; i < 4; i++) { char value[32]; snprintf(value, sizeof(value), "%llu", (unsigned long long)numbers[i]); dict_add(data, number_keys[i], value); }
   dict_add(data, "rx-modes", caps->rx_modes); dict_add(data, "tx-modes", caps->tx_modes);
   const char *bits[] = {"rx-audio", "rx-iq", "rx-waterfall", "tx-supported", "tx-audio"};
   const unsigned flags[] = {RRP_BRIDGE_RX_AUDIO, RRP_BRIDGE_RX_IQ, RRP_BRIDGE_WATERFALL, RRP_BRIDGE_TX, RRP_BRIDGE_TX_AUDIO};
   for (unsigned i = 0; i < 5; i++) dict_add(data, bits[i], caps->flags & flags[i] ? "true" : "false");
   char *json = dict2json(data);
   bool ok = json && db_bridge_hints_set(masterdb, key, json);
   free(json); g_free(key); dict_free(data);
   return ok;
#else
   (void)name; (void)driver; (void)caps;
   return true;
#endif
}
