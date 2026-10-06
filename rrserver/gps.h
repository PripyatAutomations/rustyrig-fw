//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Server GPS publication; receiver adapters feed gps.nmea.input events.
#ifndef RRSERVER_GPS_H
#define	RRSERVER_GPS_H
#include <stdbool.h>
#include <stdint.h>
bool rrserver_gps_position_parse(const char *text, int32_t *lat, int32_t *lon);
bool rrserver_gps_init(void);
void rrserver_gps_fini(void);
#endif
