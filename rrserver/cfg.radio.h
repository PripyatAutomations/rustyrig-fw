// Named radio composition. Part of RustyRig; MIT license.
#ifndef RR_CFG_RADIO_H
#define RR_CFG_RADIO_H
#include <stdbool.h>
#include <rrserver/hardware.h>
bool rr_radio_config_init(void);
/* Normalize a validated composition into existing rig configuration before
 * backend startup. Transactional: invalid references leave cfg unchanged. */
bool rr_radio_config_prepare(void);
bool rr_radio_hardware_register(rr_hardware_t *);
#endif
