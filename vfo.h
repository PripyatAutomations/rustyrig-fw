//
// VFO Object
//
// Storage and interface for Variable Frequency Oscillator objecs
//
#if	!defined(__rr_vfo_h)
#define	__rr_vfo_h
#include "config.h"
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
   MODE_FM
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

#endif	// !defined(__rr_vfo_h)
