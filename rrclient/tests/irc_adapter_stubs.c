// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Isolated transport probes do not instantiate the connection manager. */
#include <rrclient/connman.h>
__attribute__((weak))
void rrclient_connection_count_traffic(const rrconn_t *connection, uint64_t rx, uint64_t tx) {
   (void)connection;
   (void)rx;
   (void)tx;
}
