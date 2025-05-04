#if	!defined(__rr_config_h)
#define	__rr_config_h

// this lives in build/$platform/build_config.h
#include "build_config.h"

/* Don't use this, it will go away soon and be in buildconf */
#define	FEATURE_FILESYSTEM

/////////////////////
#if     defined(FEATURE_MQTT) || defined(FEATURE_HTTP)
#define USE_MONGOOSE
#endif

#endif
