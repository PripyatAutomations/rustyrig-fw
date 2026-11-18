//
// rrclient/cmd.help.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrclient_ui_help_h)
#define __rrclient_ui_help_h
#include <librustyaxe/config.h>

typedef void (*rrclient_help_line_cb)(const char *line, void *data);
/* Shared help content and privilege filtering; the caller owns presentation. */
extern void rrclient_help_render(rrclient_help_line_cb emit, void *data);
extern bool cmd_help(int argc, char **args);

#endif // !defined(__rrclient_ui_help_h)
