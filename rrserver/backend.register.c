// rrserver/backend.register.c: composition root for compiled backend types
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <rrserver/backend.h>

void rr_backend_register_builtin_types(void) {
   rr_backend_internal_register();
   rr_backend_hamlib_register();
}
