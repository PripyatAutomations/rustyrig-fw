//
// rrserver/protection.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#ifndef __rrserver_protection_h
#define __rrserver_protection_h

extern bool protection_warmup_pending(int amp_idx);
extern bool protection_lockout(const char *reason);
extern bool protection_init(void);

#endif // !__rrserver_protection_h
