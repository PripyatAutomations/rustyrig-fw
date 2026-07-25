//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rrprotocol_h)
#define	__rrprotocol_h

#include "build_config.h"
#if	defined(USE_MONGOOSE)
#include "ext/libmongoose/mongoose.h"
#endif
#include <librustyaxe/cat.h>
#include <librrprotocol/vfo.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/http.h>
#include <librrprotocol/ws.h>
#include <librrprotocol/ws.binframe.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/state.h>
#include <librrprotocol/client-flags.h>

#endif	// !defined(__rrprotocol_h)
