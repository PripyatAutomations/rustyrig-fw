//
// gui.nextion.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#if     !defined(__rr_gui_nextion_h)
#define __rr_gui_nextion_h
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern bool gui_nextion_init(void);
extern bool gui_nextion_update(void);

#endif // !defined(__rr_gui_nextion_h)
