// rrserver/backend.register.c: Register all available backends
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#include <rrserver/backend.h>
#include <rrserver/bridge.manager.h>

void rr_backend_register_builtin_types(void) {
   rr_backend_internal_register();
   rr_backend_yaesu_register();
   rr_backend_type_register(rrserver_bridge_backend_type());
}
