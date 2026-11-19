// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <glib.h>
#include <rrserver/relay.catalog.policy.h>

typedef struct {
   char *origin;
} sample_t;
static unsigned frees;

static void release(gpointer value) {
   sample_t *s = value;
   g_free(s->origin);
   g_free(s);
   frees++;
}

static const char *origin_of(gconstpointer value) {
   return ((const sample_t *)value)->origin;
}

static void add(GHashTable *map, const char *id, const char *origin) {
   sample_t *sample = g_new0(sample_t, 1);
   sample->origin = g_strdup(origin);
   g_hash_table_insert(map, g_strdup(id), sample);
}

int main(void) {
   const char *a = "00000000-0000-4000-8000-000000000001";
   const char *b = "00000000-0000-4000-8000-000000000002";
   const char *c = "00000000-0000-4000-8000-000000000003";
   assert(rr_catalog_sync_id_valid(a));
   assert(!rr_catalog_sync_id_valid(NULL));
   assert(!rr_catalog_sync_id_valid(""));
   assert(!rr_catalog_sync_id_valid("attacker-controlled-unbounded-key"));
   assert(!rr_catalog_sync_id_valid("00000000-0000-4000-8000-000000000001\nX"));
   GHashTable *map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, release);
   add(map, a, "peer-A");
   add(map, b, "peer-A");
   add(map, c, "peer-B");
   assert(rr_catalog_drop_origin(map, "peer-A", origin_of) == 2);
   assert(g_hash_table_size(map) == 1 && frees == 2);
   assert(g_hash_table_lookup(map, c));
   add(map, a, "peer-A");
   assert(rr_catalog_drop_origin(map, "peer-A", origin_of) == 1);
   assert(rr_catalog_drop_origin(map, "peer-A", origin_of) == 0);
   assert(g_hash_table_size(map) == 1);
   g_hash_table_destroy(map);
   assert(frees == 4);
   g_print("PASS: bounded mesh catalog sessions, per-peer supersession and canonical UUID keys\n");
   return 0;
}
