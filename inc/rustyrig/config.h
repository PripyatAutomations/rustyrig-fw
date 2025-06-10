//
// config.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_config_h)
#define	__rr_config_h

// this lives in build/$platform/build_config.h
#include "build_config.h"
#include "inc/dict.h"

//////////////////////////
// Feature dependencies //
//////////////////////////

// MQTT or HTTP require libmongoose
#if     defined(FEATURE_MQTT) || defined(FEATURE_HTTP)
#define USE_MONGOOSE
#endif

// POSIX always has file systems
#if	defined(HOST_POSIX)
#define	FEATURE_FILESYSTEM		/* Don't use this, it will go away soon and be in buildconf */
#endif

#endif
