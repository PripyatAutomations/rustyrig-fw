//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Process-local performance samples published as read-only discovery data.
// Detailed handler/decoder counters are compiled in only with USE_PROFILING.
// This is not a remote administration interface and exposes no paths or users.
#ifndef RRSERVER_PERFORMANCE_H
#define RRSERVER_PERFORMANCE_H
#include <librustyaxe/core.h>
typedef void (*rr_performance_emit_fn)(dict *row, void *context);
/* Called on the rrserver event loop, once every five minutes. */
void rrserver_performance_sample(void);
/* Emit owned inventory rows into either local inventory or the relay catalog. */
void rrserver_performance_collect(rr_performance_emit_fn emit, void *context);
#endif
