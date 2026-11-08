//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRSERVER_GPIO_PRIVATE_H
#define RRSERVER_GPIO_PRIVATE_H
#include <rrserver/gpio.h>
bool rr_gpio_add_line(rr_hardware_t *, const char *id, const char *path, unsigned offset, bool output, bool active_low, bool safe);
#endif
