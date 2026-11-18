//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
// Server GPS publication; receiver adapters feed gps.nmea.input events.
#ifndef RRSERVER_GPS_H
#define RRSERVER_GPS_H
#include <stdbool.h>
#include <stdint.h>
#include <librustyaxe/config.h>

static inline const char *rrserver_gps_station_position(void) {
   const char *position = cfg_get("station.gps.position");

   return position && *position ? position : cfg_get("site:coordinates");
}

bool rrserver_gps_position_parse(const char *text, int32_t *lat, int32_t *lon);
bool rrserver_gps_init(void);
/* Dynamically publish an approximate WebSDR site as a rig-scoped GPSP source. Both functions run on the server event loop, alongside the bridge manager. */
struct rr_server_rig;
bool rrserver_gps_websdr_grid(struct rr_server_rig *radio, const char *grid, const char *room);
void rrserver_gps_websdr_remove(const char *rig_uuid);
/* Add the effective known location to discovery; absence means unknown/no fix. */
void rrserver_gps_inventory_position(dict *row, const char *scope);
const char *rrserver_gps_public_scope(const char *alias);
/* Own configured location; inherited station data must not disable a rig receiver. */
bool rrserver_gps_source_fixed(const char *alias);
void rrserver_gps_fini(void);
#endif
