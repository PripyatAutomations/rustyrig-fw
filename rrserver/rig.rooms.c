//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Server-owned configuration of site and rig rooms.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/rig.config.h>
#include <rrserver/rig.registry.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/database.h>

extern struct GlobalState rig;

struct room_context {
   const char *room;
   uint32_t mask;
};

static bool collect_vfo(rr_server_vfo_t *vfo, void *user) {
   struct room_context *ctx = user;
   rr_vfo_t index;

   if (rr_server_vfo_native_index(vfo, &index) && index >= 0 && index < 32) {
      ctx->mask |= UINT32_C(1) << index;
   }
#ifdef USE_SQLITE

   if ( !db_room_vfo_add( masterdb, ctx->room, rr_server_vfo_id(vfo) ) ) {
      return true;
   }
#endif

   return false;
}

static bool room_duplicate(rr_server_rig_t *radio, void *user) {
   struct room_context *ctx = user;
   const char *room = rr_rig_registry_room(rig.rigs, radio);

   return room && !strcasecmp(room, ctx->room);
}

static bool configure_room(rr_server_rig_t *radio, void *user) {
   (void)user;
   const char *alias = rr_rig_registry_alias(rig.rigs, radio);
   char *configured = rr_rig_config_get_exp(alias, "room");
   char generated[128];
   int len = snprintf(generated, sizeof(generated), "%s-%s", ws_site_room(), alias);
   const char *room = configured && *configured ? configured : generated;
   struct room_context ctx = {
      .room = room
   };

   if ( ( !configured && ( len < 0 || (size_t)len >= sizeof(generated) ) ) ||
        !ws_room_rig_base(room) || !ws_room_rig_namespace(room) ||
        strcasecmp(room, generated) || !strcasecmp( room, ws_site_room() ) ||
        rr_rig_registry_foreach(rig.rigs, room_duplicate, &ctx) ) {
      Log(LOG_CRIT, "cfg.rig", "Invalid or duplicate room %s for rig %s", room, alias);
      free(configured);

      return true;
   }
   bool failed = rr_rig_registry_set_room(rig.rigs, radio, room);
#ifdef USE_SQLITE

   if (!failed) {
      failed = !db_room_restore(masterdb, room, "server") || !db_room_ensure(masterdb, room, true, 0, "server") ||
               !db_room_vfos_clear(masterdb, room);
   }
#endif

   if (!failed) {
      failed = rr_server_vfo_foreach(radio, collect_vfo, &ctx);
   }

   if (!failed) {
      failed = !ws_room_set_vfo_mask(room, ctx.mask);
   }
   uint32_t tuning_mask = rr_rig_config_get_bool(alias, "rx-independent-tuning", false) ? ctx.mask : 0;
   char *independent = rr_rig_config_get_exp(alias, "rx-independent-vfos");

   if (independent) {
      tuning_mask = 0;
      char *save = NULL;

      for ( char *id = strtok_r(independent, ", \t", &save) ; id ; id = strtok_r(NULL, ", \t", &save) ) {
         char native[2] = {
            (char)toupper( (unsigned char)id[0] ), 0
         };
         rr_server_vfo_t *vfo = !id[1] ? rr_server_vfo_find_alias(radio, native) : NULL;
         rr_vfo_t index;

         if (!vfo || !rr_server_vfo_native_index(vfo, &index) || index < 0 || index >= 32) {
            Log(LOG_CRIT, "cfg.rig", "Unknown independent RX VFO %s for rig %s", id, alias);
            failed = true; break;
         }
         tuning_mask |= UINT32_C(1) << index;
      }
   }
   free(independent);

   if (!failed) {
      failed = !ws_room_set_rx_tuning_mask(room, tuning_mask);
   }
#ifdef USE_SQLITE

   if (!failed) {
      failed = !db_room_ensure(masterdb, room, true, ctx.mask, "server");
   }
#endif

   if ( !failed && radio == rr_rig_registry_default(rig.rigs) ) {
      ws_set_authoritative_room(room);
      ws_set_authoritative_vfo_mask(ctx.mask);
   }

   if (!failed) {
      Log(LOG_INFO, "rig.rooms", "Rig %s uses room %s", alias, room);
   }
   free(configured);

   return failed;
}

#ifdef USE_SQLITE
static bool restore_rx_rooms(void) {
   sqlite3_stmt *statement = NULL;

   if (sqlite3_prepare_v2(masterdb, "SELECT name FROM rooms WHERE deleted=0;", -1, &statement, NULL) != SQLITE_OK) {
      return true;
   }
   bool failed = false;
   int status;
   while ( ( status = sqlite3_step(statement) ) == SQLITE_ROW && !failed ) {
      const char *room = (const char *)sqlite3_column_text(statement, 0);

      if ( rrserver_rig_room_configured(room) ) {
         continue;
      }
      rr_server_rig_t *radio = ws_room_rig_namespace(room) && !ws_room_rig_base(room)
         ? rrserver_rig_for_room(room) : NULL;
      uint32_t mask = 0;
      char *bindings = db_room_vfo_list(masterdb, room), *save = NULL;

      for ( char *id = bindings ? strtok_r(bindings, " \t\r\n", &save) : NULL ;
            id ; id = strtok_r(NULL, " \t\r\n", &save) ) {
         rr_server_vfo_t *vfo = radio ? rr_server_vfo_find_uuid(radio, id) : NULL;
         rr_vfo_t index;

         if (vfo && rr_server_vfo_native_index(vfo, &index) && index >= 0 && index < 32) {
            mask |= UINT32_C(1) << index;
         } else if ( !db_room_vfo_remove(masterdb, room, id) ) {
            failed = true;
         }
      }

      free(bindings);

      if ( !db_room_ensure(masterdb, room, mask != 0, mask, "server") ) {
         failed = true;
      }

      if ( radio && !ws_room_set_vfo_mask(room, mask) ) {
         failed = true;
      }
   }

   if (status != SQLITE_DONE && !failed) {
      failed = true;
   }
   sqlite3_finalize(statement);

   return failed;
}
#endif

bool rrserver_rig_rooms_init(void) {
   if ( !rig.rigs || !ws_room_name_valid( ws_site_room() ) ) {
      return true;
   }
#ifdef USE_SQLITE

   if (sqlite3_exec(masterdb, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK) {
      return true;
   }
   bool failed = !db_room_restore(masterdb, ws_site_room(), "server") || !db_room_ensure(masterdb, ws_site_room(), false, 0, "server") ||
                 !db_room_vfos_clear( masterdb, ws_site_room() );
#else
   bool failed = false;
#endif

   if (!failed) {
      failed = !ws_room_set_vfo_mask(ws_site_room(), 0) ||
               rr_rig_registry_foreach(rig.rigs, configure_room, NULL);
   }
#ifdef USE_SQLITE

   if (!failed) {
      failed = restore_rx_rooms();
   }

   if (sqlite3_exec(masterdb, failed ? "ROLLBACK;" : "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
      failed = true;
   }
#endif

   return failed;
}

static bool matches_room(rr_server_rig_t *radio, void *user) {
   const char *room = rr_rig_registry_room(rig.rigs, radio);

   return room && !strcasecmp(room, user);
}

bool rrserver_rig_room_configured(const char *room) {
   return room && rig.rigs && rr_rig_registry_foreach(rig.rigs, matches_room, (void *)room);
}

struct lookup_room_context {
   const char *room; rr_server_rig_t *radio;
};
static bool find_room_rig(rr_server_rig_t *radio, void *data) {
   struct lookup_room_context *ctx = data;
   const char *base = rr_rig_registry_room(rig.rigs, radio);

   if ( base && ws_room_same_rig(ctx->room, base) ) {
      ctx->radio = radio;
      return true;
   }

   return false;
}
rr_server_rig_t *rrserver_rig_for_room(const char *room) {
   struct lookup_room_context ctx = {
      .room = room
   };

   if (room && rig.rigs) {
      rr_rig_registry_foreach(rig.rigs, find_room_rig, &ctx);
   }

   return ctx.radio;
}
