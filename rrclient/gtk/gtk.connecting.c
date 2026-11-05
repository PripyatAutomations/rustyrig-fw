/* RustyRig sign-on animation. All GTK ownership stays in the GTK domain. */
#include "gtk.connecting.h"
#include "gtk.floating-panel.h"
#include <rrclient/connecting.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
   GtkWidget *canvas, *close_button;
   RRFloatPanel *panel;
   GdkPixbuf *earth, *logo;
   char *status;
   unsigned step, frame;
   guint timer;
   guint dismiss_timer;
   bool completed;
} RRConnect;
static GHashTable *dialogs;
extern GtkWidget *main_window;
extern GtkWidget *main_notebook;
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
   if(s->dismiss_timer)g_source_remove(s->dismiss_timer);
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
static void connect_panel_dismiss(GtkWidget *canvas, gboolean cancel) {
   if (!GTK_IS_WIDGET(canvas)) return;
   RRConnect *state = g_object_get_data(G_OBJECT(canvas), "rr-connect-state");
   if (!state) return;
   const char *profile = g_object_get_data(G_OBJECT(canvas), "rr-profile");
   if (cancel && profile && !state->completed) {
      rr_connect_progress_t req = {0};
      g_strlcpy(req.profile, profile, sizeof(req.profile));
      event_emit_message(&(rr_event_message_t) {
         .name = "client.connecting.cancel", .sender = "gtk",
         .payload = &req, .length = sizeof(req)
      });
   }
   /* Destroying the panel destroys the canvas, removes the hash entry and
    * cancels both GLib sources via state_free. Never touch state afterwards. */
   rr_floating_panel_destroy(state->panel);
}
static void connect_panel_close(gpointer user) {
   connect_panel_dismiss(GTK_WIDGET(user), TRUE);
}
static void connect_panel_button(GtkButton *button, gpointer user) {
   (void)button;
   connect_panel_close(user);
}
static void destroyed(GtkWidget *canvas, gpointer unused) {
   (void)unused;
   const char *profile = g_object_get_data(G_OBJECT(canvas), "rr-profile");
   if (dialogs && profile && g_hash_table_lookup(dialogs, profile) == canvas)
      g_hash_table_remove(dialogs, profile);
}
GtkWidget *rr_connect_dialog_new(GtkWindow *parent) {
   (void)parent;
   GtkWidget *root = main_window && GTK_IS_BIN(main_window) ?
      gtk_bin_get_child(GTK_BIN(main_window)) : NULL;
   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook))
      return NULL;
   /* Separate IDs allow simultaneous connection attempts to keep their
    * own panel geometry and lifecycle. */
   static guint next_panel_id;
   char *panel_id = g_strdup_printf("connecting-%u", ++next_panel_id);
   RRFloatPanel *panel = rr_floating_panel_new(GTK_OVERLAY(root),
      GTK_NOTEBOOK(main_notebook), panel_id, "RustyRig — Connecting");
   g_free(panel_id);
   if (!panel) return NULL;
   RRConnect *state = g_new0(RRConnect, 1);
   state->panel = panel;
   state->canvas = gtk_drawing_area_new();
   state->earth = load_earth();
   state->logo = load_logo();
   gtk_widget_set_size_request(state->canvas, 520, 290);
   gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(panel)),
                      state->canvas, TRUE, TRUE, 0);
   g_object_set_data_full(G_OBJECT(state->canvas), "rr-connect-state", state, state_free);
   g_signal_connect(state->canvas, "draw", G_CALLBACK(redraw), state);
   g_signal_connect(state->canvas, "destroy", G_CALLBACK(destroyed), NULL);
   rr_floating_panel_set_close_callback(panel, connect_panel_close, state->canvas);
   GtkWidget *actions = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_button_box_set_layout(GTK_BUTTON_BOX(actions), GTK_BUTTONBOX_END);
   state->close_button = gtk_button_new_with_label("Cancel");
   g_signal_connect(state->close_button, "clicked", G_CALLBACK(connect_panel_button), state->canvas);
   gtk_container_add(GTK_CONTAINER(actions), state->close_button);
   gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(panel)), actions, FALSE, FALSE, 2);
   state->timer = g_timeout_add(150, tick, state);
   rr_floating_panel_show(panel);
   return state->canvas;
}
void rr_connect_dialog_set_step(GtkWidget *canvas, unsigned step) {
   RRConnect *s = g_object_get_data(G_OBJECT(canvas), "rr-connect-state");
   if (s) { s->step = MIN(step, 3u); gtk_widget_queue_draw(s->canvas); }
}
void rr_connect_dialog_set_status(GtkWidget *canvas, const char *message) {
   RRConnect *s = g_object_get_data(G_OBJECT(canvas), "rr-connect-state");
   if (s) { g_free(s->status); s->status = g_strdup(message); gtk_widget_queue_draw(s->canvas); }
}
static gboolean finished(gpointer data) {
   GtkWidget *canvas = data;
   RRConnect *state = g_object_get_data(G_OBJECT(canvas), "rr-connect-state");
   if (state) state->dismiss_timer = 0;
   const char *profile = g_object_get_data(G_OBJECT(canvas), "rr-profile");
   if (dialogs && profile && g_hash_table_lookup(dialogs, profile) == canvas)
      connect_panel_dismiss(canvas, FALSE);
   return G_SOURCE_REMOVE;
}
static void on_progress(const rr_event_message_t *m, void *unused) {
   (void)unused;
   if (!m || !m->payload || m->length != sizeof(rr_connect_progress_t)) return;
   rr_connect_progress_t v;
   memcpy(&v, m->payload, sizeof(v));
   if (!memchr(v.profile, 0, sizeof(v.profile)) || !v.profile[0]) return;
   GtkWidget *canvas = dialogs ? g_hash_table_lookup(dialogs, v.profile) : NULL;
   if (v.phase == RR_CONNECT_PHASE_FAILED) {
      if (canvas) connect_panel_dismiss(canvas, FALSE);
      return;
   }
   /* Only actual in-progress connections create a panel. */
   if (!canvas && v.phase == RR_CONNECT_PHASE_COMPLETE) return;
   if (!canvas) {
      canvas = rr_connect_dialog_new(GTK_IS_WINDOW(main_window) ? GTK_WINDOW(main_window) : NULL);
      if (!canvas) return;
      g_object_set_data_full(G_OBJECT(canvas), "rr-profile", g_strdup(v.profile), g_free);
      g_hash_table_insert(dialogs, g_strdup(v.profile), canvas);
   }
   if (v.phase == RR_CONNECT_PHASE_LOOKUP) {
      rr_connect_dialog_set_step(canvas, 0);
      rr_connect_dialog_set_status(canvas, "Finding RustyRig server...");
   }
   if (v.phase == RR_CONNECT_PHASE_TRANSPORT) {
      rr_connect_dialog_set_step(canvas, 1);
      rr_connect_dialog_set_status(canvas, "Establishing secure connection...");
   }
   if (v.phase == RR_CONNECT_PHASE_AUTHORIZED) {
      rr_connect_dialog_set_step(canvas, 2);
      rr_connect_dialog_set_status(canvas, "Authenticated; discovering the cluster...");
   }
   if (v.phase == RR_CONNECT_PHASE_CATALOG) {
      rr_connect_dialog_set_step(canvas, 2);
      rr_connect_dialog_set_status(canvas, "Synchronizing cluster objects...");
   }
   if (v.phase == RR_CONNECT_PHASE_COMPLETE) {
      RRConnect *state = g_object_get_data(G_OBJECT(canvas), "rr-connect-state");
      if (!state || state->completed) return;
      state->completed = true;
      rr_connect_dialog_set_step(canvas, 3);
      rr_connect_dialog_set_status(canvas, "Catalog synchronized. Welcome to RustyRig!");
      gtk_button_set_label(GTK_BUTTON(state->close_button), "✕ Close");
      state->dismiss_timer = g_timeout_add(3500, finished, canvas);
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
   for (GList *p = values; p; p = p->next)
      connect_panel_dismiss(GTK_WIDGET(p->data), FALSE);
   g_list_free(values);g_hash_table_destroy(dialogs);dialogs=NULL;
}
