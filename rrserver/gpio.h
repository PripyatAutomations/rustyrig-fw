// rrserver/gpio.h: kernel GPIO controllers and explicitly configured lines
// SPDX-License-Identifier: MIT
#ifndef RRSERVER_GPIO_H
#define RRSERVER_GPIO_H
#include <rrserver/hardware.h>
/* Configuration and discovery never request lines or change pin directions.
 * Returns true on success, including a machine with no GPIO controllers. */
bool rr_gpio_config_init(void);
bool rr_gpio_discover(rr_hardware_t * registry);
bool rr_gpio_configure(rr_hardware_t * registry);
/* Only the worker holding the device lease may call these. Logical values
 * respect active-low. Read returns -1 on error, otherwise zero or one. */
int rr_gpio_read(const rr_hardware_device_t * device);
bool rr_gpio_write(const rr_hardware_device_t * device, bool active);
#endif
