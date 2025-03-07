#if	!defined(_rr_cat_yaesu_h)
#define	_rr_cat_yaesu_h
#include "config.h"

// Command lookup table
typedef struct {
    const char *command;
    uint8_t min_args;
    uint8_t max_args;
    void (*cat_yaesu_r)(const char *args);
} CATCommand;

extern bool cat_yaesu_init(void);

#endif	// !defined(_rr_cat_yaesu_h)
