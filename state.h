// This file contains the structures used for statistics and state
#if	!defined(__rr_state_h)
#define	__rr_state_h
#include "config.h"
#include <time.h>
#include <stdbool.h>
#include "logger.h"
#include "backend.h"
#include "atu.h"		// for rr_atu_tv
#define	PARSE_LINE_LEN	512

// TX Low Pass Filters
// Ideally these need to be moved to the build config
//	* Band Name, Start Freq, Stop Freq, Rolloff
enum LPFSelection {
    LPF_NONE = 0,			// No filter selected
    LPF_160M,				// 160M LPF
    LPF_80M,				// 80M LPF
    LPF_40M,				// 40M LPF
    LPF_30_20M,				// 30/20M LPF
    LPF_17_15M,				// 17/15M LPF
    LPF_12_10M,				// 12/10M LPF
    LPF_LOW_USER1,			// Low Band, User Filter 1
    LPF_LOW_USER2,			// Low Band, User Filter 2
    LPF_6M,				// 6M LPF
    LPF_2M,				// 2M LPF
    LPF_1_25M,				// 1.25M LPF
    LPF_70CM,				// 70CM LPF
    LPF_HIGH_USER1,			// High Band, User Filter 1
    LPF_HIGH_USER2			// High Band, User Filter 2
};

// Intermediate RX Band Pass Filtering, if equipped
enum BPFSelection {
    BPF_NONE = 0,			// No filter selected
    BPF_160_80M,			// 160 - 80M BPF
    BPF_60_40M,				// 60 - 40M BPF
    BPF_30_20M,				// 30 - 20M BPF
    BPF_17_15M,				// 17 - 15M BPF
    BPF_12_10M				// 12 - 10M BPF
};


// State of the amplifier module
struct AmpState {
   uint32_t   alc[MAX_BANDS];		// ALC: 0-210, per band
   uint32_t   current_band;		// Current band selection
   uint32_t   afr;			// AFR:
   bool	      inhibit;			// Inhibit TX / Locked out
   uint32_t   power;			// Power control
   uint32_t   standby;			// Standby mode
   uint32_t   output_target[MAX_BANDS];	// Target power (see formula in .c)
   float      power_target;		// Target power configuration
   float      thermal;	                // Thermal state of Final Transistor (in degF)
   bool       warmup_required;		// If true, we will enforce a warmup time
   uint32_t   warmup_time;		// Warmup time required for device
};

// State of the all tunings: PA & Matching Units
enum TuningState {
    TS_UNKNOWN = 0,
    TS_TUNING,
    TS_TUNE_FAILED,
    TS_TX_READY
};

// Antenna Matching Unit current state
struct ATUState {
   float power_fwd,			// Measured forward power
         power_rev;			// Measured reflected power
   float thermal;			// Reported temperature or -1000 if not available
   rr_atu_tv *tv;			// Active tuning values
};

struct FilterState {
   enum LPFSelection LPF;		// Chosen TX Low Pass Filter
   enum BPFSelection BPF;		// Chosen RX Band Pass Filter
   float thermal;			// Thermal state of Final Transistor
};

struct GlobalState {
   logpriority_t log_level;		// Minimum log level to show
   rr_backend_t	*backend;		// Selected backend (set in main.c)
   bool  tx_blocked;			// is TX blocked (user control)?
   bool  ptt;				// Are we transmitting?
   bool  faultbeep;			// Beep on faults
   bool  bc_standby;			// Stay in STANDBY on band change?
   uint32_t   serial;			// Device serial #
   uint8_t    fan_speed;		// Fan speed: 0-6 (0: auto)
   uint32_t   fault_code;		// Current fault code
   uint32_t   faults;			// Faults since last cleared
   uint32_t   tr_delay;			// T/R delay
   bool       eeprom_ready;		// EEPROM initialized
   bool       eeprom_dirty;		// EEPROM needs written out
   bool       eeprom_corrupted;		// EEPROM is corrupted; prompt before write out
   time_t     eeprom_saved;		// When was EEPROM last written out?
   time_t     eeprom_changed;		// When was a setting last changed that needs written to EEPROM?

   // Thermals
   float therm_inlet;			// Air inlet temp
   float therm_enclosure;		// Current temperature INSIDE box

   // Statistics
   time_t		time_tx_total,	// Lifetime TX time total
                        time_tx_last;	// Last transmission length
   float		power_tx_watts; // Lifetime total watts used transmitting

   // Sub-units
   struct AmpState 	amps[RR_MAX_AMPS];
   struct ATUState 	atus[RR_MAX_ATUS];
   struct FilterState 	filters[RR_MAX_FILTERS];

#if	defined(HOST_POSIX)   // Host build fd's/buffers/etc
   uint32_t		eeprom_fd;
   u_int8_t		*eeprom_mmap;
   size_t		eeprom_size;
   uint32_t		logfile_fd;
   uint32_t		catpipe_fd;
#endif	// defined(HOST_POSIX)
};

extern void shutdown_rig(uint32_t signum);	// main.c
extern void restart_rig(void);

////////////////////
extern int my_argc;		// in main.c
extern char **my_argv;		// in main.c

extern struct GlobalState rig;	// in main.c
extern bool dying;              // in main.c
extern bool restarting;		// in main.c
extern time_t now;		// in main.c
#endif	// !defined(__rr_state_h)
