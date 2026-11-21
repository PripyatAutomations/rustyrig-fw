// SPDX-License-Identifier: GPL-2.0-only
/* In-memory persistence for isolated GTK panel tests.
 * Real GUI code is linked; only the client SQLite settings service is mocked.
 */
#include <string.h>
#include <glib.h>
#include <rrclient/database.h>

static GHashTable *settings;
static GHashTable *windows;

char *rrclient_database_setting_get(const char *key) {
   const char *value = settings ? g_hash_table_lookup(settings, key) : NULL;
   return value ? g_strdup(value) : NULL;
}

bool rrclient_database_setting_set(const char *key, const char *value) {
   if (!key) return false;
   if (!settings)
      settings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   if (value)
      g_hash_table_replace(settings, g_strdup(key), g_strdup(value));
   else
      g_hash_table_remove(settings, key);
   return true;
}

bool rrclient_database_window_save(const char *id, const rrclient_window_state_t *state) {
   if (!id || !state) return false;
   if (!windows)
      windows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   rrclient_window_state_t *copy = g_new(rrclient_window_state_t, 1);
   *copy = *state;
   g_hash_table_replace(windows, g_strdup(id), copy);
   return true;
}

bool rrclient_database_window_load(const char *id, rrclient_window_state_t *out) {
   const rrclient_window_state_t *saved = windows && id ? g_hash_table_lookup(windows, id) : NULL;
   if (!saved || !out) return false;
   *out = *saved;
   return true;
}
