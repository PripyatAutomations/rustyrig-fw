// librrprotocol/cfg.fwdsp.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if !defined(__cfg_fwdsp_h)
#define __cfg_fwdsp_h

#include <stdbool.h>

// Register the [fwdsp] and [pipelines] configuration section handlers.
// Returns true on success, false on error.
extern bool config_fwdsp_init(void);

// Section callbacks return true when the input is invalid and false when accepted.
// [fwdsp] section callback.
// Stores entries as fwdsp:<key>.
extern bool config_fwdsp_section_cb(const char *path, int line,
   const char *section, const char *buf);

// [pipelines] section callback.
// Stores entries as pipeline:<codec>.<dir>.
extern bool config_pipeline_section_cb(const char *path, int line,
   const char *section, const char *buf);

#endif // !defined(__cfg_fwdsp_h)
