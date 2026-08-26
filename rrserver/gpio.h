//
// rrserver/gpio.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrserver_gpio_h)
#define	__rrserver_gpio_h
#include "build_config.h"
#include <stdbool.h>

#ifdef	HOST_POSIX
// XXX: Include host gpio support
extern const uint32_t max__rr_gpiochips;
#else	// HOST_POSIX
// Include uc specific gpio
#endif	// HOST_POSIX

#define	GPIO_KEYLEN 8
struct GPIO_pin {
   char key[GPIO_KEYLEN + 1];
   struct gpiod_chip    *gpiochip;
   // XXX: properties (in/out/etc)
};
typedef struct GPIO_pin GPIOpin;

typedef struct radio__rr_gpiochip {
   struct gpiod_chip    *chip;
   char key[GPIO_KEYLEN + 1];
   bool active;
} radio_gpiochip;

extern uint32_t gpio_init(void);

#endif // !defined(__rrserver_gpio_h)
