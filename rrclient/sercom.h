#ifndef RRCLIENT_SERCOM_H
#define RRCLIENT_SERCOM_H
#include <stdbool.h>
#include <rrclient/serial.h>
bool rr_sercom_init(void);
void rr_sercom_shutdown(void);
bool cmd_sercom(int argc, char **args);
bool rr_sercom_attach(const char *name, const char *service, const char *device);
bool rr_sercom_disconnect(const char *name);
const char *rr_sercom_binding(const char *name);
// Synchronous CAT context: no effect on UI selection or global VFO state.
const char *rr_cat_room(void);
const char *rr_cat_property(const char *vfo, const char *property, const char *fallback);
long rr_cat_property_long(const char *vfo, const char *property, long fallback);
bool rr_cat_property_bool(const char *property, bool fallback);
char rr_cat_active_vfo(void);
int rr_cat_serial_reply(const char *data, size_t len);
#endif
