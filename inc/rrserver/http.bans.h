// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRSERVER_HTTP_BANS_H
#define RRSERVER_HTTP_BANS_H

#include <stdbool.h>

bool is_http_banned(const char *ua);
bool load_http_ua_bans(const char *path);

#endif /* RRSERVER_HTTP_BANS_H */
