//
// rrserver/help.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrserver_help_h)
#define __rrserver_help_h
#include <librustyaxe/io.h>

extern bool send_help(rr_io_context_t *port, const char *topic);

#endif // !defined(__rrserver_help_h)
