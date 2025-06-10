//
// filters.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_filters_h)
#define	__rr_filters_h

// Filter type
#include <stdint.h>
#include "rustyrig/config.h"

#define	FILTER_NONE		0x0000
#define	FILTER_LPF		0x0001
#define	FILTER_BPF		0x0002
#define	FILTER_HPF		0x0004
#define	FILTER_NOTCH		0x0010


struct rf_filter {
    uint32_t	f_type;
};

extern int filter_init(int fid);
extern int filter_init_all(void);

#endif	// !defined(__rr_filters_h)
