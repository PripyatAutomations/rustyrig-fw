//
// rrserver/timer.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrserver_timer_h)
#define __rrserver_timer_h

extern bool timer_create_periodic(const char *name, int interval, int repeats, void (*callback) ());
extern bool timer_create_oneshot(const char *name, int delay, void (*callback) ());
extern bool timer_run(void);
extern bool timer_init(void);
extern const time_t cfg_rig_hard_tot;  // timer.clocktick.c

#endif // !defined(__rrserver_timer_h)
