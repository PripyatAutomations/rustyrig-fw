//
// amp.h: Amplifier controls
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_amp_h)
#define	__rr_amp_h

// Initialize a single amplifier (called by ..._init_all)
extern bool rr_amp_init(uint8_t index);

// Initialize all connected amplifiers
extern bool rr_amp_init_all(void);

// Called to free memory allocated by the amp subsystem
// This will be called before reloading
extern bool rr_amp_fini(void);

#endif	// !defined(__rr_amp_h)
