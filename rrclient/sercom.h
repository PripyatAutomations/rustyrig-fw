//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
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
/* GTK/TUI update CAT/GPS/modem aliases when the active rig changes. */
void rr_sercom_focus_room(const char *profile, const char *room);
/* Select an existing server ttyMODEMN as active modem for focused rig.
 * Updates the floating ttyMODEM PTY without altering numbered bindings.
 */
bool rr_sercom_select_modem(const char *port);
/* Core-thread GUI requests; response contains selected remote serial port
 * and current connection status. The server owns the modem process. */
#define RR_SERCOM_MODEM_REQUEST "client.modem.remote.request"
#define RR_SERCOM_MODEM_SNAPSHOT "client.modem.remote.snapshot"
typedef struct {
   char target[64]; /* Empty = query; ttyMODEMN = change active modem */
} rr_sercom_modem_request_t;
typedef struct {
   bool attached, success;
   char profile[128], rig[64], target[64], state[96];
} rr_sercom_modem_snapshot_t;
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
const char *rr_cat_model(void);
char rr_cat_timeout_get(void);
void rr_cat_timeout_set(char value);
int rr_cat_tx_vfo_get(void);
void rr_cat_tx_vfo_set(int value);
const char *rr_cat_property(const char *vfo, const char *property, const char *fallback);
long rr_cat_property_long(const char *vfo, const char *property, long fallback);
bool rr_cat_property_bool(const char *property, bool fallback);
char rr_cat_active_vfo(void);
int rr_cat_serial_reply(const char *data, size_t len);
#endif
