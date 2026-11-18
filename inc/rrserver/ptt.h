//
// rrserver/ptt.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#ifndef __rrserver_ptt_h
#define __rrserver_ptt_h
#include <librrprotocol/rrprotocol.h>

extern void rr_ptt_register_accounting_events(void);
extern bool rr_ptt_accounting_locked(void);
extern bool rr_ptt_check_blocked(void);
extern bool rr_ptt_set_blocked(bool blocked);
extern bool rr_ptt_set(rr_vfo_t vfo, bool ptt);
// Returns true on rejection/backend failure; legacy setters return the requested state.
extern bool rr_ptt_request(rr_vfo_t vfo, bool ptt, const char *reason);
extern bool rr_ptt_set_reason(rr_vfo_t vfo, bool ptt, const char *reason);
extern bool rr_ptt_toggle(rr_vfo_t vfo);
// False on success; failed safety releases may be retried.
extern bool rr_ptt_set_all_off(void);
extern bool rr_ptt_set_all_off_reason(const char *reason);
extern const char *rr_ptt_recording_id(rr_vfo_t vfo);
extern const char *rr_ptt_recording_file(rr_vfo_t vfo);
extern void quota_reset_warned(const char *username);

extern time_t global_tot_time;

#endif // !__rrserver_ptt_h
