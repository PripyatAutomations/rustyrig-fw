//
// filters.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rr_filters_h)
#define __rr_filters_h

// Filter type
#include <stdint.h>
#include "build_config.h"

#define FILTER_NONE 0x0000
#define FILTER_LPF 0x0001
#define FILTER_BPF 0x0002
#define FILTER_HPF 0x0004
#define FILTER_NOTCH 0x0010

// TX Low Pass Filters
// Ideally these need to be moved to the build config
// * Band Name, Start Freq, Stop Freq, Rolloff
enum LPFSelection {
   LPF_NONE = 0,                        // No filter selected
   LPF_160M,                            // 160M LPF
   LPF_80M,                             // 80M LPF
   LPF_40M,                             // 40M LPF
   LPF_30_20M,                          // 30/20M LPF
   LPF_17_15M,                          // 17/15M LPF
   LPF_12_10M,                          // 12/10M LPF
   LPF_LOW_USER1,                       // Low Band, User Filter 1
   LPF_LOW_USER2,                       // Low Band, User Filter 2
   LPF_6M,                              // 6M LPF
   LPF_2M,                              // 2M LPF
   LPF_1_25M,                           // 1.25M LPF
   LPF_70CM,                            // 70CM LPF
   LPF_HIGH_USER1,                      // High Band, User Filter 1
   LPF_HIGH_USER2                       // High Band, User Filter 2
};

// Intermediate RX Band Pass Filtering, if equipped
enum BPFSelection {
   BPF_NONE = 0,                        // No filter selected
   BPF_160_80M,                         // 160 - 80M BPF
   BPF_60_40M,                          // 60 - 40M BPF
   BPF_30_20M,                          // 30 - 20M BPF
   BPF_17_15M,                          // 17 - 15M BPF
   BPF_12_10M                           // 12 - 10M BPF
};


struct FilterState {
   enum LPFSelection LPF;                // Chosen TX Low Pass Filter
   enum BPFSelection BPF;                // Chosen RX Band Pass Filter
   float thermal;                        // Thermal state of Final Transistor
};

struct rf_filter {
   uint32_t f_type;
};

extern int filter_init(int fid);
extern int filter_init_all(void);

#endif // !defined(__rr_filters_h)
