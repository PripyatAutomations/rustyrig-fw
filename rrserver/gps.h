// Server GPS publication; receiver adapters feed gps.nmea.input events.
#ifndef RRSERVER_GPS_H
#define RRSERVER_GPS_H
#include <stdbool.h>
#include <stdint.h>
bool rrserver_gps_position_parse(const char *text, int32_t *lat, int32_t *lon);
bool rrserver_gps_init(void);
void rrserver_gps_fini(void);
#endif
