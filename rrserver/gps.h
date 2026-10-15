//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
const char *rrserver_gps_public_scope(const char *alias);
/* Own configured location; inherited station data must not disable a rig receiver. */
bool rrserver_gps_source_fixed(const char *alias);
void rrserver_gps_fini(void);
#endif
