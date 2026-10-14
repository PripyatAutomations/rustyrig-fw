#include <assert.h>
#include "rrclient/gtk/gtk.ptt-btn.c"
bool dying, restarting, ptt_active;
time_t now;
const char *login_user = "operator";
struct rr_user *global_userlist;
static char test_current_vfo = 'A';
static const char *current_room = "#rig";
char vfo_state_get_active(void) { return test_current_vfo; }
static bool legacy_cached_ptt;
bool vfo_state_get_bool(const char *vfo, const char *key, bool fallback) {
   return legacy_cached_ptt;
}
const char *rrclient_current_room(void) {
   return current_room;
}
void vfo_controls_set_ptt_state(bool locked, bool local) {
}

static rrconn_t connection = {
   .is_ws = true
};

rrconn_t *ws_conn = &connection;
int cfg_ui_ptt_ack_timeout = 2, cfg_ui_ptt_hold_delay = 250;
time_t poll_block_expire, poll_block_delay;
static bool noob, supervised, room_offline;
bool rrclient_room_offline(const char *room) { return room_offline; }
bool media_have_priv(const char *priv) {
   return noob && !strcmp(priv, "noob");
}
bool rrclient_supervision_granted(rrconn_t *connection, const char *room) {
   return supervised;
}

static unsigned ptt_sends;
bool ws_send_ptt_cmd_in_room(rrconn_t *connection, const char *vfo, bool state, const char *room) {
   (void)connection;
   (void)vfo;
   (void)state;
   (void)room;
   ptt_sends++;

   return true;
}
const char *gtk_chat_current_room(void) {
   return current_room;
}
const char *rrclient_media_current_codec(bool tx) {
   (void)tx;

   return "pc16";
}
bool audio_switch_codec(const char *codec, bool tx) {
   (void)codec;
   (void)tx;

   return false;
}
struct rr_user *userlist_find(const char *name) {
   (void)name;

   return NULL;
}
bool ui_print(const char *window, const char *format, ...) {
   (void)window;
   (void)format;

   return false;
}

int main(void) {
   ptt_sync_context();
   ptt_button_pending = true;
   ptt_button_pending_state = true;
   ptt_button_pending_expire = 102;
   now = 100;
   ptt_button_set_state(false);
   assert(ptt_button_pending);  // stale contradictory echo
   ptt_button_set_state(true);
   assert(!ptt_button_pending && ptt_active && !ptt_button_pending_expire);
   ptt_button_pending = true;
   ptt_button_pending_state = false;
   ptt_button_pending_quiet = true;
   ptt_button_pending_expire = 102;
   ptt_button_set_state(true);
   assert(ptt_button_pending && ptt_button_pending_quiet);
   ptt_button_set_state(false);
   assert(!ptt_button_pending && !ptt_active && !ptt_button_pending_quiet);
   ptt_button_pending = true;
   ptt_button_pending_expire = 102;
   ptt_button_refresh();
   assert(ptt_button_pending);
   now = 102;
   ptt_button_refresh();
   assert(!ptt_button_pending && !ptt_button_pending_expire && !ptt_active);
   ptt_button_pending = true;
   ptt_button_pending_state = true;
   ptt_button_tot_expired(60);
   assert(ptt_btn_tot && !ptt_active && !ptt_button_pending);
   ptt_button_set_state(false);
   update_ptt_button_ui(NULL, 0);
   ptt_button_refresh();
   assert(ptt_btn_tot);

   if (gtk_init_check(NULL, NULL)) {
      ptt_button = gtk_toggle_button_new_with_label("PTT OFF");
      ptt_toggled_handler = g_signal_connect(ptt_button, "toggled", G_CALLBACK(on_ptt_toggled), NULL);
      ptt_btn_online = true;
      ptt_btn_tot = false;
      noob = true;
      ptt_button_refresh();
      assert(!strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "REQUEST"));
      assert(gtk_widget_get_sensitive(ptt_button));
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), TRUE);
      assert(ptt_sends == 1 && !ptt_active && !ptt_button_pending);
      supervised = true;
      ptt_button_refresh();
      assert(!strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "PTT OFF"));
      noob = false;
      ptt_sends = 0;
      ptt_button_tot_expired(60);
      assert(!strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "TOT EXPIRE"));
      ptt_button_set_state(false);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), TRUE);
      assert(!ptt_btn_tot && !ptt_sends);
      assert(!strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "PTT OFF"));
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), TRUE);
      assert(ptt_sends == 1);
      ptt_button_tot_expired(60);
      assert(ptt_button_hotkey_press() && !ptt_btn_tot && ptt_sends == 1);
      assert(ptt_button_hotkey_press() && ptt_sends == 1);
      assert(ptt_button_hotkey_release() && ptt_sends == 1);
      struct rr_user own = {
         .name = "operator", .room = "#rig", .ptt_room = "#rig", .ptt_vfo = 'A', .is_ptt = true
      };
      global_userlist = &own;
      ptt_button_set_state(false);  // Tab refresh cleared the local widget state.
      unsigned before_restore = ptt_sends;
      ptt_button_refresh();
      assert(ptt_active && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ptt_button)));
      assert(!strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "operator"));
      assert(gtk_style_context_has_class(gtk_widget_get_style_context(ptt_button), "ptt-active"));
      assert(ptt_sends == before_restore);   // Restoring presentation sends no CAT command.
      current_room = "#other-rig";
      ptt_button_refresh();
      assert(!ptt_active && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ptt_button)));
      assert(!strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "PTT OFF"));
      assert(ptt_sends == before_restore);
      current_room = "#rig";
      ptt_button_refresh();
      assert(ptt_active && !strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "operator"));
      test_current_vfo = 'B';
      ptt_button_refresh();
      assert(!ptt_active && !strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "PTT OFF"));
      test_current_vfo = 'A';
      ptt_button_refresh();
      assert(ptt_active && ptt_sends == before_restore);
      ptt_button_pending = true;
      current_room = "#other-rig";
      ptt_button_refresh();
      assert(!ptt_active && !ptt_button_pending);
      legacy_cached_ptt = true;  // VFO A's old rig must not drive timeout recovery.
      ptt_button_pending = true;
      ptt_button_pending_expire = now;
      ptt_button_refresh();
      assert(!ptt_active && !ptt_button_pending);
      legacy_cached_ptt = false;
      current_room = "#rig";
      ptt_button_refresh();
      assert(ptt_active && ptt_sends == before_restore);
      rrconn_t other_connection = {.is_ws=true};
      global_userlist = NULL;
      ws_conn = &other_connection;
      ptt_button_refresh();
      assert(!ptt_active && !strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "PTT OFF"));
      ws_conn = &connection;
      room_offline = true;
      ptt_button_refresh();
      assert(!gtk_widget_get_sensitive(ptt_button));
      assert(!strcmp(gtk_button_get_label(GTK_BUTTON(ptt_button)), "OFFLINE"));
      assert(!ptt_button_hotkey_press());
      room_offline = false;
      ptt_button_refresh();
      assert(gtk_widget_get_sensitive(ptt_button));
      gtk_widget_destroy(ptt_button);
      ptt_button = NULL;
   }
   puts("PASS: GTK matching PTT acknowledgements, stale echoes, and timeout");
}
