//
// amp.h: Amplifier controls
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rr_amp_h)
#define __rr_amp_h

// remove from librustyaxe/cat.h ASAP
// State of the amplifier module
struct AmpState {
   uint32_t alc[MAX_BANDS];              // ALC: 0-210, per band
   uint32_t current_band;                // Current band selection
   uint32_t afr;                         // AFR:
   bool inhibit;                         // Inhibit TX / Locked out
   uint32_t power;                       // Power control
   uint32_t standby;                     // Standby mode
   uint32_t output_target[MAX_BANDS];    // Target power (see formula in .c)
   float power_target;                   // Target power configuration
   float thermal;                        // Thermal state of Final Transistor
                                         // (in
                                         // degF)
   bool warmup_required;                 // If true, we will enforce a warmup
                                         // time
   uint32_t warmup_time;                 // Warmup time required for device
};


// Initialize a single amplifier (called by ..._init_all)
extern bool rr_amp_init(uint8_t index);

// Initialize all connected amplifiers
extern bool rr_amp_init_all(void);

// Called to free memory allocated by the amp subsystem
// This will be called before reloading
extern bool rr_amp_fini(void);

#endif // !defined(__rr_amp_h)
