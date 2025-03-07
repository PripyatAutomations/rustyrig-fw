#if	!defined(__rr_gpio_h)
#define	__rr_gpio_h
#include "config.h"
#include <stdbool.h>
#if	defined(HOST_POSIX)
// XXX: Include host gpio support
extern const uint32_t max__rr_gpiochips;
#else
// Include uc specific gpio
#endif

#define	GPIO_KEYLEN	8
struct GPIO_pin {
   char 		key[GPIO_KEYLEN + 1];
   struct gpiod_chip	*gpiochip;
   // XXX: properties (in/out/etc)
};
typedef struct GPIO_pin GPIOpin;

typedef struct radio__rr_gpiochip {
   struct gpiod_chip 	*chip;
   char			key[GPIO_KEYLEN + 1];
   bool			active;
} radio_gpiochip;

extern uint32_t gpio_init(void);

#endif	// !defined(__h)

