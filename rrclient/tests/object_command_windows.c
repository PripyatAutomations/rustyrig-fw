// Command output stays with its issuing window, including delayed inventory.
#include <assert.h>
#include <stdarg.h>
#include "rrclient/objects.events.c"

bool dying, restarting;
time_t now;
static rrconn_t session;
rrconn_t *ws_conn = &session;
static const char *active = "#first";
static char destination[128], sent_id[48];
static unsigned printed;
static bool send_ok = true;
const char *ui_active_window_name(void) { return active; }
bool ui_print(const char *window, const char *fmt, ...) {
   snprintf(destination,sizeof(destination),"%s",window ? window : "");
   printed++;
   return false;
}
bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int type) {
   snprintf(sent_id,sizeof(sent_id),"%s",dict_get(d,"request.id",""));
   return send_ok;
}
const struct rr_client_media_chan *rrclient_media_chan_iter(int index, int *next) { return NULL; }
bool cmd_media(int argc, char **args) { return false; }

static void reply(const char *id, const char *cmd, const char *window) {
   dict *d = dict_new();
   dict_add(d,"object.cmd",cmd);dict_add(d,"request.id",id);
   dict_add(d,"inventory.kind","site");dict_add(d,"inventory.room",window);
   unsigned before = printed;
   assert(inventory_message(d));
   assert(printed == before + 1);
   assert(!strcmp(destination,window));
   dict_free(d);
}
static void tree_row(const char *id, unsigned depth, const char *kind, const char *room, bool visible) {
   dict *d = dict_new();
   dict_add(d,"object.cmd","inventory-entry");dict_add(d,"request.id",id);
   dict_add_uint(d,"inventory.depth",depth);dict_add(d,"inventory.kind",kind);
   if (room) dict_add(d,"inventory.room",room);
   unsigned before = printed;
   assert(inventory_message(d));assert(printed == before + visible);
   dict_free(d);
}
int main(void) {
   char *list[] = {"rig", "list"}, *objects[] = {"objects"}, *gps[] = {"gps", "bad"};
   cmd_objects(1,objects);assert(!strcmp(destination,"#first"));
   assert(!cmd_rig(2,list));
   char first[48];snprintf(first,sizeof(first),"%s",sent_id);
   active = "#second";
   assert(!cmd_rig(2,list));
   char second[48];snprintf(second,sizeof(second),"%s",sent_id);
   active = "#third";
   reply(first,"inventory-entry","#first");
   reply(second,"inventory-entry","#second");
   reply(first,"inventory-end","#first");
   reply(second,"inventory-end","#second");
   dict *late = dict_new();dict_add(late,"object.cmd","inventory-entry");dict_add(late,"request.id",first);
   unsigned before = printed;assert(inventory_message(late));assert(printed == before);dict_free(late);
   cmd_gps(2,gps);assert(!strcmp(destination,"#third"));
   send_ok = false;assert(cmd_rig(2,list));
   for (unsigned i = 0; i < INVENTORY_REQUESTS_MAX; i++) assert(!inventory_requests[i].id[0]);
   send_ok = true;assert(!cmd_rig(2,list));
   connection("disconnected",NULL,NULL,NULL);
   active="#site-rig1.rx";assert(!cmd_rig(2,list));
   snprintf(first,sizeof(first),"%s",sent_id);
   active="#site-rig10"; // Replies retain the original filter across tab switches.
   tree_row(first,0,"site","#site",false);
   tree_row(first,1,"rig","#site-rig1",true);
   tree_row(first,2,"gps",NULL,true);
   tree_row(first,1,"rig","#site-rig10",false);
   tree_row(first,2,"serial",NULL,false);
   reply(first,"inventory-end","#site-rig1.rx");
   active="#site";assert(!cmd_rig(2,list));
   snprintf(first,sizeof(first),"%s",sent_id);
   tree_row(first,0,"site","#site",true);
   tree_row(first,1,"rig","#site-rig10",true);
   tree_row(first,2,"gps",NULL,true);
   tree_row(first,0,"site","#other",false);
   tree_row(first,1,"gps",NULL,false);
   connection("disconnected",NULL,NULL,NULL);
   for (unsigned i = 0; i < INVENTORY_REQUESTS_MAX; i++) assert(!inventory_requests[i].id[0]);
   puts("PASS: native command output and concurrent inventory stay in issuing windows");
   return 0;
}
