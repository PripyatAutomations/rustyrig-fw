#if	!defined(__rr_config_h)
#define	__rr_config_h

// this lives in build/$platform/build_config.h
#include "build_config.h"


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
