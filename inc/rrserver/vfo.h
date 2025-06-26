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
#include "rrserver/config.h"
#include <stdint.h>
#include <stdbool.h>

#define	MAX_VFOS		2		// maximum VFOs
#define	DEFAULT_TOT_TIME	300		// TOT time, if not set

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
   MODE_DSB,
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
   int          width;		// width in hz
   float        freq;		// dial frequency
   rr_mode_t	mode;		// Mode we're TXing
   float        power;		// power in watts
   time_t	tx_started;
   time_t	tot_time;	// TOT time length configured (defaults to DEFAULT_TOT_TIME)
};
typedef struct rr_vfo_data rr_vfo_data_t;
extern bool set_vfo_frequency(rr_vfo_type_t vfo_type, uint32_t input, float freq);
extern rr_vfo_t vfo_lookup(const char vfo);
extern const char vfo_name(rr_vfo_t vfo);
extern rr_mode_t vfo_parse_mode(const char *mode);
extern const char *vfo_mode_name(rr_mode_t mode);
extern long parse_freq(const char *str);
extern rr_vfo_data_t get_vfo(int vfo);

//
extern rr_vfo_data_t vfos[MAX_VFOS];
extern rr_vfo_t active_vfo;

#endif	// !defined(__rr_vfo_h)
