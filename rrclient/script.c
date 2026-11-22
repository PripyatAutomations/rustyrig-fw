/* Client-owned RustyScript integration: GTK/frontend APIs and lifecycle.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <rrscript/host.h>
#include <rrclient/frontend.h>
#include <librustyscript/runtime.h>
#include <librustyaxe/core.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>

/* Scripts only see immutable IDs and properties; no GTK pointers escape.
 * These operations use the same GTK floating panel manager as drag/drop. */
static bool script_frontend_context(rrs_vm_t *vm) {
   if(!g_main_context_is_owner(g_main_context_default())) {
      rrs_vm_raise(vm,"Window operations require the GTK main thread");
      return false;
   }
   return true;
}
static rrs_value_t *native_window_find(rrs_vm_t *vm,int argc,
                                        const rrs_value_t *const argv[],void *user) {
   (void)user;
   if((argc!=1&&argc!=2) || rrs_value_type(argv[0])!=RRS_STRING ||
      (argc==2 && rrs_value_type(argv[1])!=RRS_STRING)) {
      rrs_vm_raise(vm,"window.find(pattern[, location]) expects strings");
      return NULL;
   }
   if(!script_frontend_context(vm))return NULL;
   const rr_frontend_ops_t *ops=frontend_ops();
   if(!ops||!ops->panel_find) {
      rrs_vm_raise(vm,"Panel search requires GTK frontend");
      return NULL;
   }
   char *id=NULL,*title=NULL,*where=NULL;
   const char *location=argc==2?rrs_value_string(argv[1]):"*";
   bool found=ops->panel_find(rrs_value_string(argv[0]),location,
                              &id,&title,&where);
   if(!found) {
      g_free(id);g_free(title);g_free(where);
      return rrs_null();
   }
   rrs_value_t *out=rrs_dictionary();
   rrs_value_t *value=rrs_string(id);
   bool ok=out && value && rrs_dict_put(out,"id",value);
   rrs_value_free(value);
   value=rrs_string(title?title:"");
   ok=ok&&value&&rrs_dict_put(out,"title",value);
   rrs_value_free(value);
   value=rrs_string(where?where:"M");
   ok=ok&&value&&rrs_dict_put(out,"location",value);
   rrs_value_free(value);
   g_free(id);g_free(title);g_free(where);
   if(!ok){rrs_value_free(out);rrs_vm_raise(vm,"Out of memory");return NULL;}
   return out;
}
static rrs_value_t *native_window_move(rrs_vm_t *vm,int argc,
                                        const rrs_value_t *const argv[],void *user) {
   (void)user;
   if((argc!=2 && argc!=4) || rrs_value_type(argv[0])!=RRS_STRING ||
      rrs_value_type(argv[1])!=RRS_STRING ||
      (argc==4 && (rrs_value_type(argv[2])!=RRS_INT ||
                   rrs_value_type(argv[3])!=RRS_INT))) {
      rrs_vm_raise(vm,"window.move(id, location[, x, y]) expects strings and optional coordinates");
      return NULL;
   }
   if(!script_frontend_context(vm))return NULL;
   const rr_frontend_ops_t *ops=frontend_ops();
   if(!ops||!ops->panel_move) {
      rrs_vm_raise(vm,"Panel movement requires GTK frontend");
      return NULL;
   }
   int64_t x=argc==4?rrs_value_int(argv[2]):-1;
   int64_t y=argc==4?rrs_value_int(argv[3]):-1;
   if(x>8192||y>8192||(argc==4 && (x<0||y<0))) {
      rrs_vm_raise(vm,"Panel coordinates must be 0..8192");
      return NULL;
   }
   if(!ops->panel_move(rrs_value_string(argv[0]),rrs_value_string(argv[1]),(int)x,(int)y)) {
      rrs_vm_raise(vm,"Panel not found, invalid dock, or no active chat dock");
      return NULL;
   }
   return rrs_boolean(true);
}
static rrs_value_t *native_window_percent(rrs_vm_t *vm,int argc,
                                           const rrs_value_t *const argv[],void *user) {
   if(argc!=2 || rrs_value_type(argv[0])!=RRS_STRING ||
      rrs_value_type(argv[1])!=RRS_INT) {
      rrs_vm_raise(vm,"window.zoom/opacity(id, percent) requires ID and integer");
      return NULL;
   }
   if(!script_frontend_context(vm))return NULL;
   const rr_frontend_ops_t *ops=frontend_ops();
   if(!ops){rrs_vm_raise(vm,"Panel controls require GTK frontend");return NULL;}
   int64_t value=rrs_value_int(argv[1]);
   if(value<0||value>200) {
      rrs_vm_raise(vm,"Invalid percentage");
      return NULL;
   }
   bool zoom=GPOINTER_TO_INT(user)!=0;
   bool success=zoom?(ops->panel_zoom&&ops->panel_zoom(rrs_value_string(argv[0]),(int)value)):
                     (ops->panel_opacity&&ops->panel_opacity(rrs_value_string(argv[0]),(int)value));
   if(!success) {
      rrs_vm_raise(vm,"Panel not found or percentage out of range");
      return NULL;
   }
   return rrs_boolean(true);
}
static bool register_window_api(rrs_vm_t *vm) {
   return rrs_vm_register_native(vm,"window.find",native_window_find,NULL) &&
      rrs_vm_register_native(vm,"window.move",native_window_move,NULL) &&
      rrs_vm_register_native(vm,"window.zoom",native_window_percent,GINT_TO_POINTER(1)) &&
      rrs_vm_register_native(vm,"window.opacity",native_window_percent,NULL) &&
      rrs_vm_register_native(vm,"panel.find",native_window_find,NULL) &&
      rrs_vm_register_native(vm,"panel.move",native_window_move,NULL) &&
      rrs_vm_register_native(vm,"panel.zoom",native_window_percent,GINT_TO_POINTER(1)) &&
      rrs_vm_register_native(vm,"panel.opacity",native_window_percent,NULL);
}

rrscript_host_t *rrclient_script_host;

void rrclient_script_init(void) {
   const char *path=cfg_get("security.scripting.path");
   char *default_path=NULL;
   if(!path||!*path) {
      default_path=g_build_filename(g_get_user_config_dir(),"rustyrig","scripts",NULL);
      path=default_path;
   }
   rrclient_script_host=rrscript_host_open(path,true,register_window_api);
   g_free(default_path);
}
void rrclient_script_shutdown(void) {
   rrscript_host_close(rrclient_script_host);
   rrclient_script_host=NULL;
}
bool rrclient_script_hotkey(const char *sequence) {
   return rrscript_host_hotkey(rrclient_script_host,sequence);
}
