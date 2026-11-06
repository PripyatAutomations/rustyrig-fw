//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_LOCAL_BRIDGE_H
#define RRCLIENT_LOCAL_BRIDGE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
bool rrclient_local_bridge_connect(const char *url);
bool rrclient_local_bridge_connect_driver(const char *url, const char *driver);
size_t rrclient_local_bridge_streams(char ids[][64], char vfos[], size_t capacity);
/* Cumulative bytes on local Kiwi's remote sockets (audio and waterfall).
 * Excludes bridge IPC and any server/relay transport already counted. */
void rrclient_local_bridge_traffic(uint64_t *rx, uint64_t *tx);
#define RRCLIENT_KIWI_CONTROL_EVENT "client.kiwi.control.request"
#define RRCLIENT_KIWI_STATUS_EVENT "client.kiwi.control.status"
#define RRCLIENT_KIWI_STATUS_REQUEST "client.kiwi.control.status.request"
#define RRCLIENT_LOCAL_BRIDGE_ACTIVE_EVENT "client.local-bridge.active"
#define RRCLIENT_LOCAL_BRIDGE_ACTIVE_REQUEST "client.local-bridge.active.request"
#define RRCLIENT_LOCAL_BRIDGE_TUNE_REQUEST "client.local-bridge.tune.request"
typedef enum {
   RR_LOCAL_TUNE_FREQUENCY = 1,
   RR_LOCAL_TUNE_MODE,
   RR_LOCAL_TUNE_WIDTH
} rr_local_tune_kind_t;
typedef struct {
   rr_local_tune_kind_t kind;
   char vfo; /* Zero selects the core-owned active local VFO. */
   uint64_t hz;
   char value[32];
} rr_local_tune_request_t;
/* Posts an owned copy; success means queued, not that the receiver accepted tuning. */
bool rrclient_local_bridge_request_tune(rr_local_tune_kind_t kind, char vfo, uint64_t hz, const char *value);
/* GTK reads copies of published core-owned bridge state; no borrowed pointers. */
typedef struct {
   char vfo;
   bool connected, waterfall;
   uint64_t frequency_hz;
   char mode[16];
   unsigned min_zoom, max_zoom;
} rr_local_bridge_ui_state_t;
bool rrclient_local_bridge_ui_state(rr_local_bridge_ui_state_t *out);
bool rrclient_local_bridge_ui_state_vfo(char vfo, rr_local_bridge_ui_state_t *out);
bool rrclient_local_bridge_ui_connected(const char *url);
#define RRCLIENT_LOCAL_BRIDGE_WATERFALL_REQUEST "client.local-bridge.waterfall.request"
typedef struct {
   bool enabled;
   unsigned zoom;
   uint64_t center_hz;
} rr_local_bridge_waterfall_request_t;
bool rrclient_local_bridge_request_waterfall(bool enabled, unsigned zoom, uint64_t center_hz);

#define RRCLIENT_LOCAL_BRIDGE_LIFECYCLE_REQUEST "client.local-bridge.lifecycle.request"
typedef struct {
   char url[1024];
   char driver[32];
   bool disconnect;
} rrclient_local_bridge_lifecycle_request_t;
typedef struct {
   char vfo;
   bool connected;
   char driver[32];
} rrclient_local_bridge_active_t;
typedef struct {
   char vfo;
   char control[32];
   char value[32];
} rrclient_kiwi_control_request_t;
typedef struct {
   char vfo;
   bool available;
   uint64_t frequency_hz;
   char mode[16];
} rrclient_kiwi_control_status_t;
void rrclient_local_bridge_controls_init(void);
bool rrclient_local_bridge_connected(const char *url);
bool rrclient_local_bridge_active(void);
bool rrclient_local_bridge_waterfall(bool enabled, unsigned zoom);
bool rrclient_local_bridge_waterfall_at(bool enabled, unsigned zoom, uint64_t center_hz);
bool rrclient_local_bridge_waterfall_caps(unsigned *min_zoom, unsigned *max_zoom);
bool rrclient_local_bridge_state(uint64_t *hz, const char **mode);
bool rrclient_local_bridge_state_vfo(char vfo, uint64_t *hz, const char **mode);
bool rrclient_local_bridge_tune(uint64_t hz, const char *mode);
bool rrclient_local_bridge_tune_vfo(char vfo, uint64_t hz, const char *mode);
bool rrclient_local_bridge_set_frequency(char vfo, uint64_t hz);
bool rrclient_local_bridge_set_mode(char vfo, const char *mode);
bool rrclient_local_bridge_set_width(char vfo, const char *width);
void rrclient_local_bridge_disconnect_url(const char *url);
void rrclient_local_bridge_disconnect(void);
/* Returns true if a local ! command was recognized and consumed. */
bool rrclient_local_bridge_command(const char *line);
bool rrclient_local_bridge_cli(int argc, char **argv);
#endif
