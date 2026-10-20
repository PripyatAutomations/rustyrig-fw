//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// SPDX-License-Identifier: MIT
#ifndef RRSERVER_GPIO_PRIVATE_H
#define RRSERVER_GPIO_PRIVATE_H
#include <rrserver/gpio.h>
bool rr_gpio_add_line(rr_hardware_t *, const char *id, const char *path, unsigned offset, bool output, bool active_low, bool safe);
#endif
