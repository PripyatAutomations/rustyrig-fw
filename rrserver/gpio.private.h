// SPDX-License-Identifier: MIT
#ifndef RRSERVER_GPIO_PRIVATE_H
#define RRSERVER_GPIO_PRIVATE_H
#include <rrserver/gpio.h>
bool rr_gpio_add_line(rr_hardware_t *, const char *id, const char *path, unsigned offset, bool output, bool active_low, bool safe);
#endif
