// Real internal/Hamlib registry diagnostic; no client discovery required.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/backend.h>
#include <rrserver/database.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.config.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.registry.h>

extern defconfig_t defcfg[];
time_t now;
bool dying;
bool restarting;
struct GlobalState rig;
static unsigned broadcasts;
static long internal_frequency = 14074000;

void shutdown_rig(uint32_t signal) {
   (void)signal;
   dying = true;
}

void ws_broadcast_dict(rrconn_t *sender, dict *message, int type) {
   (void)sender;
   (void)type;
   assert(!strcmp(dict_get(message, "msg.type", ""), "cat"));
   assert(dict_get_long(message, "cat.state.freq", 0) == internal_frequency);
   broadcasts++;
}

#ifndef RR_TEST_OBJECT_PROTOCOL
bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *message, int type) {
   (void)sender;
   (void)dest;
   (void)message;
   (void)type;

   return false;
}
#endif

static bool ignore_section(const char *path, int line, const char *section, const char *buf) {
   (void)path;
   (void)line;
   (void)section;
   (void)buf;

   return false;
}

static void property_event(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)data;
   (void)client;
   (void)user;
}

static rr_property_snapshot_t snapshot(rr_server_vfo_t *vfo, const char *property) {
   rr_property_snapshot_t value = {
      0
   };
   assert(rr_vfo_property_read(vfo, property, &value));

   return value;
}

#ifdef RR_TEST_OBJECT_PROTOCOL
#include "object_protocol.h"
#endif

static bool dump_vfo(rr_server_vfo_t *vfo, void *user) {
   const char *alias = user;
   assert(rr_server_vfo_lifecycle(vfo) == RR_VFO_PERSISTENT);
   assert(g_uuid_string_is_valid(rr_server_vfo_id(vfo)));
   rr_property_snapshot_t freq = snapshot(vfo, RR_PROP_VFO_FREQUENCY);
   rr_property_snapshot_t mode = snapshot(vfo, RR_PROP_VFO_MODE);
   printf("VFO %s %s %s frequency=%ld known=%d available=%d mode=%s mode-available=%d\n", alias, rr_server_vfo_alias(vfo), rr_server_vfo_id(vfo), freq.known ?
      freq.value.l : 0, freq.known, freq.available, mode.known ? mode.value.s : "unknown", mode.available);

   return false;
}

static bool dump_rig(rr_server_rig_t *radio, void *user) {
   (void)user;
   const char *alias = rr_rig_registry_alias(rig.rigs, radio);
   printf("RIG %s %s %s default=%d\n", alias, rr_server_rig_id(radio), rr_server_rig_backend(radio)->type->name, radio == rr_rig_registry_default(rig.rigs));
   assert(g_uuid_string_is_valid(rr_server_rig_id(radio)));
   assert(rr_server_vfo_count(radio) == 2);

   return rr_server_vfo_foreach(radio, dump_vfo, (void *)alias);
}

int main(int argc, char **argv) {
   assert(argc == 3); // config and a disposable diagnostic database
   setvbuf(stdout, NULL, _IOLBF, 0);
   now = time(NULL);
   default_cfg = dict_new();
   assert(default_cfg && cfg_set_defaults(default_cfg, defcfg));
   cfg = dict_new();
   assert(rr_rig_config_init());
   assert(cfg_add_callback(NULL, "fwdsp", ignore_section));
   assert(cfg_add_callback(NULL, "pipelines", ignore_section));
   dict *loaded = cfg_load(argv[1]);
   assert(loaded);
   dict_free(cfg);
   cfg = loaded;
   dict_add(cfg, "path.db.master.template", "sql/sqlite.master.sql");
   dict_add(cfg, "path.db.master.preload", "sql/sqlite.master.preload.sql");
   masterdb = db_open(argv[2]);
   assert(masterdb);
   event_init();
   event_on(RR_PROPERTY_CHANGED_EVENT, property_event, NULL);
   assert(!rr_backend_init());
   assert(rr_rig_registry_count(rig.rigs) == 2);
   rr_server_rig_t *r0 = rr_rig_registry_find_alias(rig.rigs, "rig0");
   rr_server_rig_t *r1 = rr_rig_registry_find_alias(rig.rigs, "rig1");
   assert(r0 && r1 && r0 != r1);
   assert(strcmp(rr_server_rig_id(r0), rr_server_rig_id(r1)));
   assert(rr_rig_registry_default(rig.rigs) == r0);
   assert(rr_server_rig_backend(r0)->type == &rr_backend_internal);
   rr_backend_t *hamlib = rr_server_rig_backend(r1);
   assert(hamlib->type == &rr_backend_hamlib);
   printf("CONFIG rig1 model=%d device=%s baud=%d reconnect=%d\n", rr_backend_config_get_int(hamlib, "hamlib.model", 2), rr_backend_config_get(hamlib,
      "hamlib.device"), rr_backend_config_get_int(hamlib, "hamlib.baud", 38400), rr_backend_config_get_int(hamlib, "reconnect-interval", 30));
   rr_server_vfo_t *a0 = rr_server_vfo_find_alias(r0, "A");
   rr_server_vfo_t *a1 = rr_server_vfo_find_alias(r1, "A");
   assert(a0 && a1 && strcmp(rr_server_vfo_id(a0), rr_server_vfo_id(a1)));
   printf("NODE %s\n", rr_rig_registry_node(rig.rigs));
   assert(!rr_rig_registry_foreach(rig.rigs, dump_rig, NULL));
#ifdef RR_TEST_OBJECT_PROTOCOL
   object_net_init();
#endif
   puts("READY");

   char command[32];
   while (fgets(command, sizeof(command), stdin)) {
      if (!strncmp(command, "quit", 4)) {
         break;
      }
      bool offline = !strncmp(command, "offline", 7);
      bool observe = !strncmp(command, "observe", 7);
      // Advance the existing project clock to exercise scheduled retries.
      now += 31;
      internal_frequency += 1000;
      assert(!rr_freq_set(VFO_A, (int)internal_frequency));
      assert(!rr_freq_set(VFO_B, (int)internal_frequency));
      rr_backend_poll_all();
      assert(!dying);
      rr_property_snapshot_t f0 = snapshot(a0, RR_PROP_VFO_FREQUENCY);
      rr_property_snapshot_t f1 = snapshot(a1, RR_PROP_VFO_FREQUENCY);
      rr_property_snapshot_t m1 = snapshot(a1, RR_PROP_VFO_MODE);
      assert(f0.known && f0.available && f0.value.l == internal_frequency);
      assert(vfos[VFO_A].freq == internal_frequency);
      assert(vfos[VFO_B].freq == internal_frequency);
      assert(broadcasts > 0);

      if (!offline && !observe) {
         assert(f1.known && f1.available && f1.value.l > 0);
         assert(m1.known && m1.available && *m1.value.s);
      }

      for (const char *alias = "AB" ; *alias ; alias++) {
         char name[2] = {
            *alias, 0
         };
         rr_server_vfo_t *vfo = rr_server_vfo_find_alias(r1, name);
         assert(vfo && rr_server_vfo_owner(vfo) == r1);
         rr_property_snapshot_t freq = snapshot(vfo, RR_PROP_VFO_FREQUENCY);
         rr_property_snapshot_t mode = snapshot(vfo, RR_PROP_VFO_MODE);
         rr_property_snapshot_t width = snapshot(vfo, RR_PROP_VFO_WIDTH);

         if (offline) {
            assert(!freq.available && !mode.available && !width.available);
         } else if (!observe) {
            assert(freq.available && mode.available && width.available);
            assert(freq.value.l == (*alias == 'A' ? 145000000 : 146000000));
            assert(!strcmp(mode.value.s, "FM"));
         }
      }

      unsigned before = broadcasts;
      rr_vfo_data_t legacy = vfos[VFO_A];
      rr_backend_poll_rig(r1, a1);
      assert(broadcasts == before);
      assert(!memcmp(&legacy, &vfos[VFO_A], sizeof(legacy)));
      assert(!rr_rig_registry_foreach(rig.rigs, dump_rig, NULL));
#ifdef RR_TEST_OBJECT_PROTOCOL
      static bool validated;

      if (!validated && !offline) {
         object_validate(r0, r1, true);
         validated = true;
      } else if (offline) {
         object_validate(r0, r1, false);
      } else {
         object_pump();
      }
#endif
      printf("PASS: %s", command);
   }
#ifdef RR_TEST_OBJECT_PROTOCOL
   object_net_fini();
#endif
   rr_backend_fini();
   assert(!rig.rigs && !rig.default_cat);
   event_shutdown();
   sqlite3_close(masterdb);
   masterdb = NULL;
   cfg_fini();

   return 0;
}
