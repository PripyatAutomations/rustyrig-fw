// rrserver/rig.compat.c: temporary cat.state output adapter
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/rig.compat.h>

extern time_t now;

struct rr_cat_compat {
   rr_server_rig_t *rig;              // borrowed; rig outlives this adapter
   rr_cat_compat_ops_t ops;
   dict *last_state[MAX_VFOS];
   time_t last_sent[MAX_VFOS];
   bool probed[MAX_VFOS];
};

static bool rr_cat_property_name(char *name, size_t len, rr_vfo_t vfo,
   const char *field) {
   if (vfo < VFO_A || vfo >= MAX_VFOS) {
      return false;
   }
   return rr_property_vfo_name(name, len, (char)('A' + vfo), field);
}

static long rr_cat_property_long(rr_cat_compat_t *adapter, rr_vfo_t vfo,
   const char *field, long fallback) {
   char name[RR_PROPERTY_NAME_MAX];
   rr_property_snapshot_t snapshot;

   if (!rr_cat_property_name(name, sizeof(name), vfo, field) ||
       !rr_rig_property_read(adapter->rig, name, &snapshot) ||
       !snapshot.known) {
      return fallback;
   }
   if (snapshot.value_type == VAL_LONG) {
      return snapshot.value.l;
   }
   if (snapshot.value_type == VAL_INT) {
      return snapshot.value.i;
   }
   return fallback;
}

static const char *rr_cat_property_string(rr_cat_compat_t *adapter,
   rr_vfo_t vfo, const char *field, const char *fallback) {
   char name[RR_PROPERTY_NAME_MAX];
   rr_property_snapshot_t snapshot;

   if (!rr_cat_property_name(name, sizeof(name), vfo, field) ||
       !rr_rig_property_read(adapter->rig, name, &snapshot) ||
       !snapshot.known || snapshot.value_type != VAL_STR) {
      return fallback;
   }
   return snapshot.value.s;
}

static bool rr_cat_vfo_supported(rr_cat_compat_t *adapter, rr_vfo_t vfo) {
   if (!adapter || !adapter->ops.vfo_supported) {
      return vfo == VFO_A || vfo == VFO_B;
   }
   return adapter->ops.vfo_supported(adapter->rig, vfo, adapter->ops.user);
}

static dict *rr_cat_build_state(rr_cat_compat_t *adapter, rr_vfo_t vfo) {
   if (!adapter || vfo < VFO_A || vfo >= MAX_VFOS) {
      return NULL;
   }

   dict *d = dict_new();
   if (!d) {
      return NULL;
   }
   rrconn_t *talker = adapter->ops.talker_get ?
      adapter->ops.talker_get(adapter->rig, adapter->ops.user) : NULL;
   bool ptt = adapter->ops.ptt_get ?
      adapter->ops.ptt_get(adapter->rig, vfo, adapter->ops.user) : false;
   const char *fallback_mode = vfo_mode_name(vfos[vfo].mode);

   dict_add(d, "msg.type", "cat");
   dict_add(d, "cat.state.vfo", vfo_name(vfo));
   dict_add_bool(d, "cat.state.active", vfo == active_vfo);
   dict_add(d, "cat.state.mode", rr_cat_property_string(adapter, vfo,
      RR_PROP_VFO_MODE, fallback_mode));

   if (adapter->ops.widths_get) {
      int widths[8];
      int count = adapter->ops.widths_get(adapter->rig, vfo, widths, 8,
         adapter->ops.user);
      if (count > 0) {
         char list[128] = { 0 };
         size_t used = 0;
         for (int i = 0; i < count && used < sizeof(list); i++) {
            int written = snprintf(list + used, sizeof(list) - used,
               "%s%d", i ? "," : "", widths[i]);
            if (written < 0 || (size_t)written >= sizeof(list) - used) {
               used = sizeof(list);
               break;
            }
            used += (size_t)written;
         }
         if (used < sizeof(list)) {
            dict_add(d, "cat.state.widths", list);
         }
      }
   }

   dict_add(d, "cat.user", talker ? talker->chatname : "");
   dict_add_int(d, "cat.state.width", (int)rr_cat_property_long(adapter, vfo,
      RR_PROP_VFO_WIDTH, vfos[vfo].width));
   dict_add_int(d, "cat.state.power", (int)vfos[vfo].power);
   dict_add_bool(d, "cat.state.ptt", ptt);
   dict_add_long(d, "cat.state.freq", rr_cat_property_long(adapter, vfo,
      RR_PROP_VFO_FREQUENCY, vfos[vfo].freq));
   dict_add_ulong(d, "msg.ts", now);
   return d;
}

/* Match the smaller pre-poll state historically synthesized by Hamlib. */
static dict *rr_cat_build_initial_state(rr_cat_compat_t *adapter,
   rr_vfo_t vfo) {
   if (!adapter || vfo < VFO_A || vfo >= MAX_VFOS) {
      return NULL;
   }
   dict *d = dict_new();
   if (!d) {
      return NULL;
   }
   bool ptt = adapter->ops.ptt_get ?
      adapter->ops.ptt_get(adapter->rig, vfo, adapter->ops.user) : false;
   dict_add(d, "msg.type", "cat");
   dict_add(d, "cat.state.vfo", vfo_name(vfo));
   dict_add_bool(d, "cat.state.active", vfo == active_vfo);
   dict_add(d, "cat.state.mode", rr_cat_property_string(adapter, vfo,
      RR_PROP_VFO_MODE, vfo_mode_name(vfos[vfo].mode)));
   dict_add_int(d, "cat.state.width", (int)rr_cat_property_long(adapter, vfo,
      RR_PROP_VFO_WIDTH, vfos[vfo].width));
   dict_add_long(d, "cat.state.freq", rr_cat_property_long(adapter, vfo,
      RR_PROP_VFO_FREQUENCY, vfos[vfo].freq));
   dict_add_bool(d, "cat.state.ptt", ptt);
   dict_add_ulong(d, "msg.ts", now);
   return d;
}

static dict *rr_cat_comparable_state(dict *state) {
   if (!state) {
      return NULL;
   }
   dict *out = dict_new();
   if (!out) {
      return NULL;
   }
   dict_add_long(out, "cat.state.freq",
      dict_get_long(state, "cat.state.freq", 0));
   dict_add(out, "cat.state.mode",
      dict_get(state, "cat.state.mode", "NONE"));
   dict_add_int(out, "cat.state.width",
      dict_get_int(state, "cat.state.width", 0));
   dict_add_bool(out, "cat.state.ptt",
      dict_get_bool(state, "cat.state.ptt", false));
   dict_add_bool(out, "cat.state.active",
      dict_get_bool(state, "cat.state.active", false));
   dict_add(out, "cat.user", dict_get(state, "cat.user", ""));
   return out;
}

static bool rr_cat_state_changed(dict *previous, dict *current) {
   if (!previous || !current) {
      return true;
   }
   dict *old_cmp = rr_cat_comparable_state(previous);
   dict *new_cmp = rr_cat_comparable_state(current);
   if (!old_cmp || !new_cmp) {
      dict_free(old_cmp);
      dict_free(new_cmp);
      return true;
   }
   dict *difference = dict_diff(old_cmp, new_cmp);
   bool changed = !difference || difference->fill > 0;
   dict_free(difference);
   dict_free(old_cmp);
   dict_free(new_cmp);
   return changed;
}

rr_cat_compat_t *rr_cat_compat_new(rr_server_rig_t *rig,
   const rr_cat_compat_ops_t *ops) {
   if (!rig) {
      return NULL;
   }
   rr_cat_compat_t *adapter = calloc(1, sizeof(*adapter));
   if (!adapter) {
      return NULL;
   }
   adapter->rig = rig;
   if (ops) {
      adapter->ops = *ops;
   }
   return adapter;
}

void rr_cat_compat_free(rr_cat_compat_t *adapter) {
   if (!adapter) {
      return;
   }
   for (int i = 0; i < MAX_VFOS; i++) {
      dict_free(adapter->last_state[i]);
   }
   free(adapter);
}

void rr_cat_compat_prepare_poll(rr_cat_compat_t *adapter, rr_vfo_t vfo) {
   if (!adapter || vfo < VFO_A || vfo >= MAX_VFOS ||
       adapter->probed[vfo]) {
      return;
   }
   adapter->probed[vfo] = true;
   if (vfo != active_vfo && vfos[vfo].freq == 0 &&
       active_vfo >= VFO_A && active_vfo < MAX_VFOS &&
       vfos[active_vfo].freq > 0) {
      vfos[vfo] = vfos[active_vfo];
      vfos[vfo].id = vfo;
      Log(LOG_DEBUG, "backend", "Default-rig VFO %s seeded from active VFO %s",
         vfo_name(vfo), vfo_name(active_vfo));
   }
}

bool rr_cat_compat_publish(rr_cat_compat_t *adapter, rr_vfo_t vfo,
   int unchanged_interval) {
   if (!adapter || vfo < VFO_A || vfo >= MAX_VFOS) {
      return true;
   }
   if (unchanged_interval < 0) {
      unchanged_interval = 15;
   }

   dict *state = rr_cat_build_state(adapter, vfo);
   if (!state) {
      return true;
   }
   bool changed = rr_cat_state_changed(adapter->last_state[vfo], state);
   if (!changed && adapter->last_sent[vfo] + unchanged_interval > now) {
      dict_free(state);
      return false;
   }

   dict *saved = dict_new();
   if (!saved || dict_merge(saved, state) != 0) {
      dict_free(saved);
      dict_free(state);
      return true;
   }
   dict_free(adapter->last_state[vfo]);
   adapter->last_state[vfo] = saved;
   adapter->last_sent[vfo] = now;
   ws_broadcast_dict(NULL, state, WEBSOCKET_OP_TEXT);
   dict_free(state);
   return false;
}

bool rr_cat_compat_send_state(rr_cat_compat_t *adapter, rrconn_t *cptr) {
   if (!adapter || !cptr) {
      return true;
   }

   for (int i = 0; i < MAX_VFOS; i++) {
      if (!rr_cat_vfo_supported(adapter, (rr_vfo_t)i)) {
         continue;
      }
      dict *state = dict_new();
      if (state && adapter->last_state[i]) {
         if (dict_merge(state, adapter->last_state[i]) != 0) {
            dict_free(state);
            state = NULL;
         }
      } else if (state) {
         dict_free(state);
         state = rr_cat_build_initial_state(adapter, (rr_vfo_t)i);
      }
      if (!state) {
         continue;
      }
      dict_add_ulong(state, "msg.ts", now);
      ws_send_dict(NULL, cptr, state, WEBSOCKET_OP_TEXT);
      dict_free(state);
   }
   return false;
}
