#include <assert.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/rooms.h>
#include <rrclient/frontend.h>
bool dying, restarting;
time_t now;
rrconn_t *ws_conn;
const char *ui_active_window_name(void) {
   return "#remote";
}
bool rrclient_present_context(void) {
   return true;
}
const rr_frontend_ops_t *frontend_ops(void) {
   return NULL;
}
void tui_update_input_line(void) {
}

extern char input_buf[TUI_INPUTLEN];
extern int tui_input_len;
static unsigned submitted;
static bool submit(const char *text) {
   submitted++;

   return false;
}
int main(void) {
   assert(rrclient_room_join("#remote"));
   rrclient_room_set_control_flags("#remote", true, true);
   rrclient_room_set_offline("#remote", true);
   assert(!rrclient_room_tx_control("#remote") && !rrclient_room_rx_tunable("#remote"));
   tui_input_blocked_cb = rrclient_chat_input_blocked;
   tui_readline_cb = submit;
   tui_window_t window = {
      0
   };
   strcpy(input_buf, "unfinished draft");
   tui_input_len = strlen(input_buf);
   handle_enter_key(&window, 3);
   assert(!submitted && !strcmp(input_buf, "unfinished draft") && tui_input_len == 16);
   strcpy(input_buf, "/part");
   tui_input_len = strlen(input_buf);
   handle_enter_key(&window, 0);
   assert(submitted == 1 && !tui_input_len && !*input_buf);
   rrclient_room_set_offline("#remote", false);
   assert(rrclient_room_tx_control("#remote") && rrclient_room_rx_tunable("#remote"));
   strcpy(input_buf, "send after recovery");
   tui_input_len = strlen(input_buf);
   handle_enter_key(&window, 0);
   assert(submitted == 2 && !tui_input_len);
   rrclient_rooms_clear();
   puts("PASS: offline TUI retains chat draft, permits slash commands and restores room controls");
}
