// Dummy attached devices for incremental radio bring-up. Part of RustyRig; MIT.
#ifndef RR_HARDWARE_DUMMY_H
#define RR_HARDWARE_DUMMY_H
#include <stdint.h>
#include <rrserver/hardware.h>
bool rr_dummy_register(rr_hardware_t *, const char *id, const char *kind, bool fail_claim, bool fail_write);
/* Eight numbered simulated controls; oscillator values are Hz, GPIO is 0/1.
 * State is per device and only accessed by its owning worker. Release zeros it. */
bool rr_dummy_write(rr_hardware_device_t *, unsigned channel, uint64_t value);
bool rr_dummy_read(const rr_hardware_device_t *, unsigned channel, uint64_t *value);
#endif
