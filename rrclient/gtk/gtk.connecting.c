/* RustyRig sign-on animation. All GTK ownership stays in the GTK domain. */
#include "gtk.connecting.h"
#include <rrclient/connecting.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
   GtkWidget *canvas;
   GdkPixbuf *earth, *logo;
   char *status;
   unsigned step, frame;
   guint timer;
} RRConnect;
static GHashTable *dialogs;
extern GtkWidget *main_window;
extern bool rr_module_token_add(rr_event_token_t token);
static void color(cairo_t *c,double r,double g,double b){cairo_set_source_rgb(c,r,g,b);}
static void fill(cairo_t *c,double x,double y,double w,double h,double r,double g,double b){
   color(c,r,g,b);cairo_rectangle(c,x,y,w,h);cairo_fill(c);
}
static void line(cairo_t *c,double x,double y,double X,double Y){
   cairo_move_to(c,x,y);cairo_line_to(c,X,Y);cairo_stroke(c);
}
static void label(cairo_t *c,const char *s,double x,double y,double sz,bool bold){
   cairo_select_font_face(c,"Sans",CAIRO_FONT_SLANT_NORMAL,bold?CAIRO_FONT_WEIGHT_BOLD:CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size(c,sz);color(c,.05,.12,.30);cairo_move_to(c,x,y);cairo_show_text(c,s);
}
static void tower(cairo_t *c,unsigned tick) {
   cairo_save(c);cairo_translate(c,75,51);
   cairo_set_line_width(c,3);color(c,.03,.12,.35);
   line(c,0,-20,-15,20);line(c,0,-20,15,20);line(c,-15,20,15,20);
   line(c,-11,10,11,10);line(c,-7,0,7,0);line(c,-4,-10,4,-10);line(c,0,-29,0,-20);
   for(int i=0;i<3;i++){
      double p=fmod((tick%24)/24.0+i*.32,1.0),r=8+p*18;
      cairo_set_source_rgba(c,.08,.29,.84,1-p*.80);
      cairo_set_line_width(c,2);
      cairo_arc(c,0,-29,r,-.95,-.24);cairo_stroke(c);
      cairo_arc(c,0,-29,r,M_PI+.24,M_PI+.95);cairo_stroke(c);
   }
   cairo_restore(c);
}
static void ht(cairo_t *c,double x,double y,double k){
   cairo_save(c);cairo_translate(c,x,y);cairo_scale(c,k,k);
   cairo_set_line_width(c,2);color(c,.08,.10,.20);line(c,3,-13,7,-32);
   fill(c,-8,-14,17,30,.13,.19,.32);
   fill(c,-5,-10,11,7,.7,.84,.72);fill(c,-5,0,11,3,.31,.34,.42);
   fill(c,-5,6,11,3,.65,.67,.72);
   cairo_restore(c);
}
static void rig(cairo_t *c,double x,double y,bool base){
   cairo_save(c);cairo_translate(c,x,y);
   double w=base?34:29,h=base?23:16;
   fill(c,-w/2,-h/2,w,h,.14,.19,.34);
   fill(c,-w/2+3,-h/2+4,w*.45,h*.39,.66,.88,.72);
   cairo_arc(c,w/2-7,1,3,0,2*M_PI);color(c,.9,.9,.88);cairo_fill(c);
   cairo_restore(c);
}
static void laptop(cairo_t *c,double x,double y){
   cairo_save(c);cairo_translate(c,x,y);
   fill(c,-17,-18,34,27,.12,.16,.26);fill(c,-14,-15,28,21,.43,.68,.87);
   color(c,.09,.12,.25);cairo_move_to(c,-17,10);cairo_line_to(c,17,10);
   cairo_line_to(c,23,17);cairo_line_to(c,-23,17);cairo_close_path(c);cairo_fill(c);
   color(c,.82,.84,.89);line(c,-12,13,12,13);cairo_restore(c);
}
static void runner(cairo_t *c,unsigned t) {
   double a=t%2?1:-1;
   cairo_save(c);cairo_translate(c,76,44);
   color(c,1,.82,.08);cairo_arc(c,0,-19,7,0,2*M_PI);cairo_fill(c);
   cairo_set_line_cap(c,CAIRO_LINE_CAP_ROUND);cairo_set_line_width(c,5);
   line(c,0,-9,0,8);line(c,0,-4,-16,3*a);line(c,0,-4,15,-9-3*a);
   line(c,0,8,-15,23-5*a);line(c,0,8,16,20+5*a);
   ht(c,16,-12-3*a,.56);
   cairo_restore(c);
}
static void cluster(cairo_t *c,unsigned t) {
   static const double p[][2]={{37,46},{73,20},{111,33},{34,76},{77,71},{114,73}};
   int count=1+(t/4)%6;
   cairo_set_line_width(c,1.3);
   for(int i=1;i<count;i++){color(c,.27,.42,.78);line(c,p[0][0],p[0][1],p[i][0],p[i][1]);}
   laptop(c,p[0][0],p[0][1]);
   for(int i=1;i<count;i++){
      if(i==2)ht(c,p[i][0],p[i][1],.62);
      else rig(c,p[i][0],p[i][1],i>=4);
   }
}
static gboolean redraw(GtkWidget *widget,cairo_t *c,gpointer user){
   RRConnect *s=user;
   GtkAllocation a;gtk_widget_get_allocation(widget,&a);
   double z=MIN(a.width/520.0,a.height/290.0);
   cairo_save(c);cairo_translate(c,(a.width-520*z)/2,(a.height-290*z)/2);cairo_scale(c,z,z);
   fill(c,0,0,520,290,.96,.96,.94);
   fill(c,10,7,500,24,.85,.85,.84);label(c,"Connecting To Rusty Rig Remote...",90,25,17,true);
   fill(c,12,33,496,2,.40,.40,.40);
   if(s->logo){
      gdk_cairo_set_source_pixbuf(c,s->logo,173,51);
      cairo_rectangle(c,173,51,64,64);cairo_fill(c);
   }
   label(c,"RUSTY RIG",244,76,23,true);
   label(c,"Remote",244,105,25,false);
   for(int i=0;i<3;i++){
      int x=20+i*169;
      fill(c,x+3,125,151,94,.30,.30,.30);fill(c,x,122,151,94,.01,.12,.33);
      fill(c,x+5,127,141,84,.77,.80,.97);fill(c,x+8,130,135,78,.84,.85,.98);
      cairo_save(c);cairo_rectangle(c,x+8,130,135,78);cairo_clip(c);
      cairo_translate(c,x+8,130);
      if((unsigned)i<=s->step){
         if(i==0){
            if(s->earth){gdk_cairo_set_source_pixbuf(c,s->earth,91,4);cairo_paint(c);}
            tower(c,s->frame);
         }else if(i==1)runner(c,s->frame);
         else cluster(c,s->frame);
      }
      cairo_restore(c);
   }
   label(c,s->status?s->status:"Looking for RustyRig server...",25,247,13,false);
   fill(c,10,258,500,2,.56,.56,.56);
   cairo_restore(c);return FALSE;
}
static gboolean tick(gpointer user){RRConnect *s=user;s->frame++;gtk_widget_queue_draw(s->canvas);return G_SOURCE_CONTINUE;}
static void state_free(gpointer data){
   RRConnect *s=data;
   if(s->timer)g_source_remove(s->timer);
   g_clear_object(&s->logo);g_clear_object(&s->earth);g_free(s->status);g_free(s);
}
static GdkPixbuf *load_logo(void) {
   /* Prefer the exact same icon already loaded for the main window. */
   if (main_window && GTK_IS_WINDOW(main_window)) {
      GdkPixbuf *icon = gtk_window_get_icon(GTK_WINDOW(main_window));
      if (icon) {
         return g_object_ref(icon);
      }
   }

   const char *paths[] = {
      "./res/rustyrig.png",
      "./rustyrig.png",
      "/usr/local/share/icons/hicolor/48x48/apps/rustyrig.png",
      "/usr/share/icons/hicolor/48x48/apps/rustyrig.png",
      "/usr/share/pixmaps/rustyrig.png",
      NULL
   };
   for (size_t i = 0; paths[i]; ++i) {
      GdkPixbuf *icon = gdk_pixbuf_new_from_file_at_scale(paths[i],
                                                            64, 64, TRUE, NULL);
      if (icon) {
         return icon;
      }
   }
   return gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                                   "rustyrig", 64, 0, NULL);
}
static GdkPixbuf *load_earth(void){
   const char *encoded="/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAAwADADASIAAhEBAxEB/8QAGgAAAwEAAwAAAAAAAAAAAAAABAYHBQIDCP/EADIQAAIBAwIEBQIDCQAAAAAAAAECAwQFEQAhEhMxQQYHUWFxgZEUFSIkMkJDkqHB0fD/xAAYAQEBAQEBAAAAAAAAAAAAAAADAgQBAP/EAB4RAAICAwEAAwAAAAAAAAAAAAECAAMREiExBCJB/9oADAMBAAIRAxEAPwDz/rb8PeFLr4lmdaGDEMe8k8h4UT69z7DQ1itL3m6R0qhuADjkK9Qo6/6+urjboDaqWipIVldn4eXDTABYhgnBBB9iSckk76VUGpdvJBbuoi1b/KWy8pGrr60x/mNTvGiofQcWSfTtoeq8tPD1TRTyWu81dPURRiUx1iKyspOAcqBj4023COuutZTQ1FKJF3URqiqF2PbGDt1z6aCvtFc7VJHDZ6mnETqy8FNUK3JAG6+vfrkami1LTgLJs2QyOXiw19klC1UY5bHCSoco/wAH/B31maqNXdI62nNsriJqeKBI5pP32aQZyeL2JGCPTU6udA1trnp2YOuAyOOjqRkHS3U6dHk9XZtw+xq8HW6WSx3G4QTRxvFIitxSBTw4yOEHqc4+2mSr8S3w0yuVEEfLXgkXJk26vnbJO2lHwjH+No6+iiP7SgE8ak7OoBDD56H6a16aorCixxKxhbpGJcjPqATrVUAahM7kiwzW/Mvz9aShq7nNb5I4SgdUJWYk/pJYHYZ/7OgI7PcK2Q00wqoaYyhZXAJiB7tjb2PTTbQ2t6iqjp6yV1kEQ4FIVgMDfZgR9tG3G4W6xRmhnt9W6ugJkiYLk+hPQZ9j01kZx8cfSJoXOWk4p7fxXGOjgT8WysxAiBPMAz01k+LQGit7snBJGrwNnY4U5Gf6iNMkyNLHJVyvJSMpJQxNhsZ2BI676TvEErtNEkjFnwXZick59ftpTct1JacRCjiA2q5TWm5Q1sBPHG2cA4yO4zqopOl/anultaMM6iOqEYCye7cI/i6Zx899SLRlsu1dZ6sVNBUvBKO6nY+xHQ6Gq0pz8jPXtK5Qw1luucYmjlrFBJBeXgcfAOxP10RcL4yQ1FPX2/MPO5dOr8SmVcAjHfOT20kr5nVtRE0dzttJVqxBbhHLLYHsD/bGhq/zAqKqGBILfBGYMcuSZjM6Y6YLaqxksGDOKrL5Nqrukllhq2rQohngMcNO2Cc5+O3XOpzUTvUzvNIcsxydcqqsqK6dp6qZ5ZG6sxzro0bOMaqMCUq46fZ//9k=";
   gsize len=0;guchar *buf=g_base64_decode(encoded,&len);
   GdkPixbufLoader *loader=gdk_pixbuf_loader_new_with_type("jpeg",NULL);
   GdkPixbuf *pix=NULL;
   if(loader && gdk_pixbuf_loader_write(loader,buf,len,NULL) &&
      gdk_pixbuf_loader_close(loader,NULL)){
      GdkPixbuf *original=gdk_pixbuf_loader_get_pixbuf(loader);
      if(original)pix=gdk_pixbuf_scale_simple(original,36,36,GDK_INTERP_BILINEAR);
   }
   if(!pix)g_warning("RustyRig connecting dialog: unable to decode embedded Earth image");
   if (loader) {
      g_object_unref(loader);
   }
   g_free(buf);
   return pix;
}
GtkWidget *rr_connect_dialog_new(GtkWindow *parent){
   GtkWidget *d=gtk_dialog_new_with_buttons("Rusty Rig Remote",parent,
      GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,NULL);
   RRConnect *s=g_new0(RRConnect,1);
   s->canvas=gtk_drawing_area_new();s->earth=load_earth();s->logo=load_logo();
   gtk_widget_set_size_request(s->canvas,520,290);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))),s->canvas,TRUE,TRUE,0);
   g_object_set_data_full(G_OBJECT(d),"rr-connect-state",s,state_free);
   g_signal_connect(s->canvas,"draw",G_CALLBACK(redraw),s);
   s->timer=g_timeout_add(150,tick,s);gtk_widget_show_all(d);return d;
}
void rr_connect_dialog_set_step(GtkWidget *d,unsigned step){
   RRConnect *s=g_object_get_data(G_OBJECT(d),"rr-connect-state");
   if(s){s->step=MIN(step,3u);gtk_widget_queue_draw(s->canvas);}
}
void rr_connect_dialog_set_status(GtkWidget *d,const char *message){
   RRConnect *s=g_object_get_data(G_OBJECT(d),"rr-connect-state");
   if(s){g_free(s->status);s->status=g_strdup(message);gtk_widget_queue_draw(s->canvas);}
}
static void destroyed(GtkWidget *d,gpointer unused){
   (void)unused;const char *profile=g_object_get_data(G_OBJECT(d),"rr-profile");
   if(dialogs && profile)g_hash_table_remove(dialogs,profile);
}
static void response(GtkDialog *d,gint code,gpointer unused){
   (void)unused;const char *profile=g_object_get_data(G_OBJECT(d),"rr-profile");
   if(profile && (code==GTK_RESPONSE_CANCEL||code==GTK_RESPONSE_DELETE_EVENT)){
      rr_connect_progress_t req={0};
      snprintf(req.profile,sizeof(req.profile),"%s",profile);
      event_emit_message(&(rr_event_message_t){
         .name="client.connecting.cancel",.sender="gtk",.payload=&req,.length=sizeof(req)
      });
   }
   gtk_widget_destroy(GTK_WIDGET(d));
}
static gboolean finished(gpointer data){
   GtkWidget *d=data;const char *p=g_object_get_data(G_OBJECT(d),"rr-profile");
   if(dialogs && p && g_hash_table_lookup(dialogs,p)==d)gtk_widget_destroy(d);
   return G_SOURCE_REMOVE;
}
static void on_progress(const rr_event_message_t *m,void *unused){
   (void)unused;
   if(!m||!m->payload||m->length!=sizeof(rr_connect_progress_t))return;
   rr_connect_progress_t v;memcpy(&v,m->payload,sizeof(v));
   if(!memchr(v.profile,0,sizeof(v.profile))||!v.profile[0])return;
   GtkWidget *d=dialogs?g_hash_table_lookup(dialogs,v.profile):NULL;
   if(v.phase==RR_CONNECT_PHASE_FAILED){if(d)gtk_widget_destroy(d);return;}
   /* The first progress notification can be delayed or skipped by a transport.
    * Later phases must still be able to bring up the sign-on window. */
   if (!d && v.phase == RR_CONNECT_PHASE_COMPLETE)
      return;
   if(!d){
      d=rr_connect_dialog_new(GTK_IS_WINDOW(main_window)?GTK_WINDOW(main_window):NULL);
      g_object_set_data_full(G_OBJECT(d),"rr-profile",g_strdup(v.profile),g_free);
      g_hash_table_insert(dialogs,g_strdup(v.profile),d);
      g_signal_connect(d,"destroy",G_CALLBACK(destroyed),NULL);
      g_signal_connect(d,"response",G_CALLBACK(response),NULL);
   }
   if(v.phase==RR_CONNECT_PHASE_LOOKUP){rr_connect_dialog_set_step(d,0);rr_connect_dialog_set_status(d,"Finding RustyRig server...");}
   if(v.phase==RR_CONNECT_PHASE_TRANSPORT){rr_connect_dialog_set_step(d,1);rr_connect_dialog_set_status(d,"Establishing secure connection...");}
   if(v.phase==RR_CONNECT_PHASE_AUTHORIZED){rr_connect_dialog_set_step(d,2);rr_connect_dialog_set_status(d,"Authenticated; discovering the cluster...");}
   if(v.phase==RR_CONNECT_PHASE_CATALOG){rr_connect_dialog_set_step(d,2);rr_connect_dialog_set_status(d,"Synchronizing cluster objects...");}
   if(v.phase==RR_CONNECT_PHASE_COMPLETE){
      rr_connect_dialog_set_step(d,3);rr_connect_dialog_set_status(d,"Catalog synchronized. Welcome to RustyRig!");
      g_timeout_add_full(G_PRIORITY_DEFAULT,1100,finished,g_object_ref(d),g_object_unref);
   }
}
void gtk_connecting_init(void){
   if(dialogs)return;
   dialogs=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
   rr_event_token_t token=event_on_message_domain_token(RR_CONNECT_PROGRESS_EVENT,on_progress,NULL,"gtk");
   if(token)rr_module_token_add(token);
}
void gtk_connecting_shutdown(void){
   if(!dialogs)return;
   GList *values=g_hash_table_get_values(dialogs);
   for(GList *p=values;p;p=p->next)gtk_widget_destroy(GTK_WIDGET(p->data));
   g_list_free(values);g_hash_table_destroy(dialogs);dialogs=NULL;
}
