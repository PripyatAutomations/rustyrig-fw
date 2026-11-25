/* SPDX-License-Identifier: GPL-2.0-only */
#include <librustyaxe/core.h>
#include <rrclient/ui.layout.h>
#include <glib.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

rrui_form_t *rrclient_ui_layout_load(const char *file,rrui_kind_t root,
   const rrclient_ui_required_t *required,size_t count,rrui_action_fn action) {
   if(!file||!*file||strchr(file,'/')||strchr(file,'\\'))return NULL;
   char *paths[]={
      g_build_filename(g_get_user_config_dir(),"rustyrig","scripts","client","ui",file,NULL),
      g_build_filename("/etc/rustyrig/scripts/client/ui",file,NULL),
      g_build_filename("/usr/share/rustyrig/scripts/client/ui",file,NULL)
   };
   rrui_form_t *out=NULL;
   for(size_t i=0;i<G_N_ELEMENTS(paths)&&!out;i++){
      struct stat st;
      if(stat(paths[i],&st)){
         if(errno!=ENOENT&&errno!=ENOTDIR)Log(LOG_WARN,"ui.layout","Cannot access %s",paths[i]);
         continue;
      }
      if(!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>65536){
         Log(LOG_WARN,"ui.layout","Rejected nonregular or oversized layout %s",paths[i]);
         continue;
      }
      char *source=NULL;gsize length=0;
      if(!g_file_get_contents(paths[i],&source,&length,NULL)){
         Log(LOG_WARN,"ui.layout","Cannot read %s",paths[i]);continue;
      }
      rrui_layout_t *l=rrui_layout_new(RRUI_TUI);
      const char *reason="Invalid layout source";
      if(length<=65536&&strlen(source)==length){
         if(rrui_layout_load(l,source)){
            const rrui_widget_t *first=rrui_layout_at(l,0);
            bool ok=first&&first->kind==root&&!*first->parent&&first->visible;
            reason="Incorrect root or required widget missing, hidden or invalid";
            for(size_t j=1;j<rrui_layout_count(l);j++)
               if(!*rrui_layout_at(l,j)->parent)ok=false;
            for(size_t j=0;j<count;j++){
               const rrui_widget_t *w=rrui_layout_find(l,required[j].id);
               if(!w||w->kind!=required[j].kind||(required[j].masked&&!w->masked))ok=false;
            }
            if(ok)out=rrui_form_new(l,action,NULL);
         }else reason=rrui_layout_error(l);
      }
      if(out)Log(LOG_INFO,"ui.layout","Loaded %s",paths[i]);
      else Log(LOG_WARN,"ui.layout","Rejected %s: %s; trying next location",paths[i],reason);
      rrui_layout_free(l);g_free(source);
   }
   for(size_t i=0;i<G_N_ELEMENTS(paths);i++)g_free(paths[i]);
   if(!out)Log(LOG_WARN,"ui.layout","No usable layout for %s",file);
   return out;
}
