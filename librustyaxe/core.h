//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__librustyaxe_core_h)
#define	__librustyaxe_core_h
#include <stdbool.h>
#include <stdint.h>
#include <librustyaxe/cat.h>
//#include <librustyaxe/event-bus.h>
//#include <librustyaxe/subproc.h>
#include <librustyaxe/config.h>
#include <librustyaxe/daemon.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/eeprom.h>
#include <librustyaxe/fwdsp-shared.h>
#include <librustyaxe/io.h>
#include <librustyaxe/io.serial.h>
#include <librustyaxe/io.socket.h>
#include <librustyaxe/irc.h>
#include <librustyaxe/event-bus.h>
#include <librustyaxe/kvstore.h>
#include <librustyaxe/json.h>
#include <librustyaxe/list.h>
#include <librustyaxe/logger.h>
#include <librustyaxe/maidenhead.h>
#include <librustyaxe/module.h>
#include <librustyaxe/posix.h>
#include <librustyaxe/ringbuffer.h>
#include <librustyaxe/tui.h>
#include <librustyaxe/util.file.h>
#include <librustyaxe/util.math.h>
#include <librustyaxe/util.string.h>
#include <librustyaxe/util.time.h>

static inline bool toggle(bool *v) {
   *v = !v;
   return *v;
}

#endif	// !defined(__librustyaxe_core_h)
