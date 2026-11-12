// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_LOCAL_MODEM_H
#define RRCLIENT_LOCAL_MODEM_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
/* Nonblocking PCM tap: worker receives 16 kHz mono signed 16-bit samples.
 * Runs on either local SDR or remote receiver decoded PCM callback thread.
 */
void rr_local_modem_feed(const char *source, const int16_t *pcm, size_t frames);
void rr_local_modem_shutdown(void);
bool cmd_local_modem(int argc, char **argv);
#endif
