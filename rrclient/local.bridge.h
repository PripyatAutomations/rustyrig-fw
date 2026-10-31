#ifndef RRCLIENT_LOCAL_BRIDGE_H
#define RRCLIENT_LOCAL_BRIDGE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
bool rrclient_local_bridge_connect(const char *url);
bool rrclient_local_bridge_connect_driver(const char *url, const char *driver);
size_t rrclient_local_bridge_streams(char ids[][64], char vfos[], size_t capacity);
bool rrclient_local_bridge_connected(const char *url);
bool rrclient_local_bridge_active(void);
bool rrclient_local_bridge_waterfall(bool enabled, unsigned zoom);
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
