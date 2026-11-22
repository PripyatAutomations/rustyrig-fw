/* Server-owned RustyScript integration; no client frontend bindings.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <rrscript/host.h>
#include <librustyaxe/core.h>

rrscript_host_t *rrserver_script_host;
void rrserver_script_init(void) {
   const char *path=cfg_get("security.scripting.path");
   rrserver_script_host=rrscript_host_open(
      path&&*path?path:"/etc/rustyrig/scripts",false,NULL);
}
void rrserver_script_shutdown(void) {
   rrscript_host_close(rrserver_script_host);
   rrserver_script_host=NULL;
}
