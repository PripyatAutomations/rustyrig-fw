//
// vfo.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// VFO Object
//
// Storage and interface for Variable Frequency Oscillator objecs
//
#if	!defined(__rr_vfo_h)
#define	__rr_vfo_h
#include "inc/config.h"
#include <stdint.h>
#include <stdbool.h>

#define	MAX_VFOS	4

typedef enum rr_vfo_type {
   VFO_INVALID = 0,	// Not present
   VFS_DDS,		// Direct Digital Synthesizer
   VFO_EXTERNAL		// External frequency reference
} rr_vfo_type_t;

enum rr_vfo {
   VFO_NONE = -1,
   VFO_A,
   VFO_B,
   VFO_C,
   VFO_D,
   VFO_E
};
typedef enum rr_vfo rr_vfo_t;

enum rr_mode {
   MODE_NONE = -1,
   MODE_CW,
   MODE_AM,
   MODE_LSB,
   MODE_USB,
   MODE_ISB,
   MODE_FM,
   MODE_DU,
   MODE_DL,
   MODE_FT4,			// ft8lib?
   MODE_FT8			// ft8lib?
};
typedef enum rr_mode rr_mode_t;

struct rr_vfo_data {
   rr_vfo_t	id;
   rr_vfo_type_t type;
   uint32_t	input;		// input #
   float        freq;				// dial frequency
   rr_mode_t	mode;				// Mode we're TXing
};
typedef struct rr_vfo_data rr_vfo_data_t;
extern bool set_vfo_frequency(rr_vfo_type_t vfo_type, uint32_t input, float freq);
extern rr_vfo_t vfo_lookup(const char *vfo);

#endif	// !defined(__rr_vfo_h)
