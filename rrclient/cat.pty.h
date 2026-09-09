//
// cat.pty.h: PTY-based CAT interface for rrclient
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// This provides a local PTY (e.g. ./dev/ttyCAT0) that external rig-control
// software (hamlib, rigctl, WSJT-X, etc) can open as a serial port.  Bytes
// written to the PTY slave are fed through the rrclient CAT parsers; replies
// written with cat_pty_printf() come back out of the slave.
//
#ifndef _rr_cat_pty_h
#define _rr_cat_pty_h

#include <stdbool.h>
#include <stddef.h>

// Initialize the CAT PTY (no-op unless cat.pty.enable is true)
extern bool cat_pty_init(void);

// Write a formatted reply out the PTY slave side
extern int cat_pty_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Is the CAT PTY up?
extern bool cat_pty_active(void);

// Master fd to write replies to (or -1 if the PTY is down)
extern int cat_pty_fd(void);

// Shut down the CAT PTY (removes the symlink, closes the master)
extern void cat_pty_shutdown(void);

#endif // !defined(_rr_cat_pty_h)
