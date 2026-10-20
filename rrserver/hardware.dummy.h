//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Dummy attached devices for incremental radio bring-up. Part of RustyRig; MIT.
#ifndef RR_HARDWARE_DUMMY_H
#define RR_HARDWARE_DUMMY_H
#include <stdint.h>
#include <rrserver/hardware.h>
bool rr_dummy_register(rr_hardware_t *, const char *id, const char *kind, bool fail_claim, bool fail_write);
/* Eight numbered simulated controls; oscillator values are Hz, GPIO is 0/1. State is per device and only accessed by its owning worker. Release zeros it. */
bool rr_dummy_write(rr_hardware_device_t *, unsigned channel, uint64_t value);
bool rr_dummy_read(const rr_hardware_device_t *, unsigned channel, uint64_t *value);
#endif
