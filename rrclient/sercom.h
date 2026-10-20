//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_SERCOM_H
#define RRCLIENT_SERCOM_H
#include <stdbool.h>
#include <rrclient/serial.h>
typedef struct {
   const char *name, *service, *profile, *path, *state;
} rr_sercom_info_t;
/* Borrowed strings, valid until the attachment changes or is disconnected. */
bool rr_sercom_info(const char *name, rr_sercom_info_t *info);
bool rr_sercom_init(void);
/* GTK/TUI may update the default CAT/GPS endpoints when the active rig changes. */
void rr_sercom_focus_room(const char *profile, const char *room);
void rr_sercom_shutdown(void);
bool cmd_sercom(int argc, char **args);
bool rr_sercom_attach(const char *name, const char *service, const char *device);
bool rr_sercom_disconnect(const char *name);
const char *rr_sercom_binding(const char *name);
/* Common serial-console interface, independent of GTK/VTE and usable by TUI. */
typedef void (*rr_sercom_console_fn)(const char *name, const char *bytes, size_t len, bool incoming, void *user);
typedef struct rr_sercom_observer rr_sercom_observer_t;
rr_sercom_observer_t *rr_sercom_observe(const char *name, rr_sercom_console_fn callback, void *user);
void rr_sercom_unobserve(rr_sercom_observer_t *observer);
/* Inject serial input without requiring an external PTY opener. */
bool rr_sercom_console_send(const char *name, const char *bytes, size_t len);
/* Physical device line settings; host: sends a configure request to the server. */
bool rr_sercom_line_settings(const char *name, rr_serial_settings_t *settings);
bool rr_sercom_set_line_settings(const char *name, const rr_serial_settings_t *settings);

// Synchronous CAT context: no effect on UI selection or global VFO state.
const char *rr_cat_room(void);
const char *rr_cat_property(const char *vfo, const char *property, const char *fallback);
long rr_cat_property_long(const char *vfo, const char *property, long fallback);
bool rr_cat_property_bool(const char *property, bool fallback);
char rr_cat_active_vfo(void);
int rr_cat_serial_reply(const char *data, size_t len);
#endif
