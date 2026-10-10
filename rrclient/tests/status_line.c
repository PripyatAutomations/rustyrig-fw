#include <assert.h>
#include "rrclient/vfo.c"
#include "rrclient/ui.statusbar.c"
// The frontend ops table: the test runs headless, so a NULL ops table is
// exactly what vfo_update_ui must handle.
#include "rrclient/frontend.c"

bool dying, restarting;
time_t now;
enum GuiMode ui_mode = UI_MODE_NONE;
const char *login_user = "operator";
static rrconn_t connection = {
   .is_ws = true
};
rrconn_t *ws_conn = &connection;
struct rr_user *global_userlist = NULL;
char sb_online[128], sb_window[128], sb_vfo[32];
void tui_refresh_sb_window(void) {
}
void tui_refresh_sb_vfo(void) {
}
void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {
}
const char *rrclient_media_current_codec(bool tx) {
   return tx ? NULL : "opuT";
}
// Exercise the real per-room selection state used by vfo.c.
const char *rrclient_media_active_room(void) {
   return "#rig";
}
const char *rrclient_media_vfo_uuid(const char *room, char vfo) {
   return NULL;
}
const dict *rrclient_object_property(const char *uuid, const char *name) {
   return NULL;
}
static unsigned redraws;
static char rejoin_targets[4][128];
static unsigned rejoin_count;
bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *message, int data_type) {
   assert(!sender && dest == ws_conn && data_type == WEBSOCKET_OP_TEXT);
   assert(!strcmp(dict_get(message, "talk.cmd", ""), "join"));
   assert(rejoin_count < 4);
   snprintf(rejoin_targets[rejoin_count++], sizeof(rejoin_targets[0]), "%s", dict_get(message, "talk.target", ""));

   return true;
}
static char *render(tui_window_t *win) {
   redraws++;

   return rrclient_tui_topline(win);
}
static void state(const char *vfo, long frequency, const char *mode) {
   dict *d = dict_new();
   dict_add_long(d, "cat.state.freq", frequency);
   dict_add(d, "cat.state.mode", mode);
   dict_add_long(d, "cat.state.width", 2700);
   dict_add_long(d, "cat.state.power", 25);
   dict_add_bool(d, "cat.state.ptt", false);
   vfo_set_dict(vfo, d);
   dict_free(d);
}
static void check(tui_window_t *win, const char *format, const char *expected) {
   dict_add(cfg, "tui.status-line", format);
   char *line = rrclient_tui_topline(win);

   if (!line || strcmp(line, expected)) {
      fprintf(stderr, "template: %s\nexpected: %s\nactual: %s\n", format, expected, line ? line : "(null)");
   }
   assert(line && !strcmp(line, expected));
   free(line);
}
static void capture_reset(FILE *file) {
   fflush(stdout);
   assert(!ftruncate(fileno(file), 0));
   rewind(file);
}
static char *capture_read(FILE *file) {
   fflush(stdout);
   long size = lseek(fileno(file), 0, SEEK_END);
   assert(size >= 0);
   char *text = calloc(1, size + 1);
   rewind(file);
   assert(fread(text, 1, size, file) == (size_t)size);

   return text;
}
/* This single-server fixture uses unqualified conversation names. */
const char *rrclient_window_room(const char *window) {
   return window;
}

int main(void) {
   cfg = dict_new();
   default_cfg = dict_new();
   dict_add(default_cfg, "tui.status-line", RRCLIENT_DEFAULT_STATUS_LINE);
   assert(rrclient_room_set_vfos("#rig", "rig0.vfo_a"));
   server_name = "station";
   ws_connected = 1;
   cfg_tui_colors = false;
   tui_window_t window = {
      0
   };
   strcpy(window.title, "chat");
   strcpy(window.status_line, "Test topic");
   char *initial = rrclient_tui_topline(&window);
   assert(!strcmp(initial, "Test topic | ONLINE"));
   free(initial);
   strcpy(window.title, "#side");
   char *room_line = rrclient_tui_topline(&window);
   assert(room_line && !strcmp(room_line, "Test topic | ONLINE"));
   free(room_line);
   strcpy(window.title, "host log");
   char *log_line = rrclient_tui_topline(&window);
   assert(log_line && !strcmp(log_line, "Test topic | ONLINE"));
   free(log_line);
   strcpy(window.title, "#rig");
   state("A", 7200123, "LSB");
   state("B", 14250000, "USB");
   check(&window, "${active_vfo} ${vfo_a_freq} ${vfo_a_freq_hz} ${vfo_a_freq_khz} ${vfo_a_freq_mhz}", "A 7200123 7200123 7200.123 7.200123");
   check(&window, "${vfo_B_mode} ${active_mode} ${active_width} ${active_power} ${active_ptt}", "USB LSB 2700 25 RX");
   check(&window, "${ptt-state}", "PTT: OFF");
   state("B", 14250000, "USB");
   dict_add(cfg, "tui.status-line", "${ptt-state}");
   dict *tx = dict_new();
   dict_add_bool(tx, "cat.state.ptt", true);
   vfo_set_dict("B", tx);
   dict_free(tx);
   vfo_state_set_active("B");
   check(&window, "${ptt-state}", "PTT: operator");
   ws_connected = -1;
   check(&window, "${ptt-state}", "PTT: WAIT");
   ws_connected = 1;
   vfo_state_set_active("B");
   dict *selection = dict_new();
   dict_add_bool(selection, "cat.state.active", true);
   vfo_set_dict("A", selection);
   assert(vfo_state_get_active() == 'B'); // physical poll cannot steal selection
   dict_add_bool(selection, "cat.state.selected", true);
   vfo_set_dict("A", selection);
   assert(vfo_state_get_active() == 'A');
   vfo_set_dict("B", selection);
   assert(vfo_state_get_active() == 'B');
   dict_free(selection);
   check(&window, "${active_vfo}/${active_freq}/${active_mode}", "B/14250000/USB");
   check(&window, "${window}/${topic}/${server}/${user}/${connection}/${rxcodec}/${txcodec}", "#rig/Test topic/station/operator/ONLINE/opuT/NONE");
   check(&window, "${missing} ${vfo_z_freq:unknown} ${vfo_z_mode:---} 100%", " unknown --- 100%");
   check(&window, "broken ${active_vfo", "broken ${active_vfo");
   check(&window, "literal {json: value} ${active_vfo}", "literal {json: value} B");
   check(&window, "", "");
   cfg_tui_colors = true;
   check(&window, "\00304${active_vfo}\017", "\033[91mB\033[0m");
   cfg_tui_colors = false;

   // Exercise the real redraw path and prove the bottom status is independent.
   int saved_stdout = dup(STDOUT_FILENO);
   FILE *output = tmpfile();
   assert(output && dup2(fileno(output), STDOUT_FILENO) >= 0);
   setvbuf(output, NULL, _IONBF, 0);
   tui_window_init();
   tui_window_create("#rig");
   tui_window_focus("#rig");
   tui_set_topline_renderer(render);
   dict_add(cfg, "tui.status-line", "TOPMARK ${active_vfo} ${vfo_a_freq}");
   tui_redraw_screen();
   tui_update_status(tui_active_window(), "BOTTOM-SENTINEL");
   char *screen = capture_read(output);
   assert(strstr(screen, "\033[1;1H TOPMARK B 7200123"));
   const char *bottom = strstr(screen, "\033[23;1H");
   assert(bottom && strstr(bottom, "BOTTOM-SENTINEL"));
   assert(!strstr(screen, " \033[24;1H\033[2K"));
   free(screen);

   capture_reset(output);
   tui_redraw_request();
   tui_redraw_flush();
   assert(!tui_redraw_if_pending());
   screen = capture_read(output);
   assert(strstr(screen, "\033[H\033[2J"));
   assert(strstr(screen, "\033[1;1H TOPMARK B 7200123"));
   free(screen);

   capture_reset(output);
   tui_redraw_defer();
   tui_print(tui_active_window(), "CHAT-SENTINEL");
   screen = capture_read(output);
   assert(!strstr(screen, "CHAT-SENTINEL"));
   free(screen);
   capture_reset(output);
   tui_redraw_flush();
   screen = capture_read(output);
   assert(strstr(screen, "CHAT-SENTINEL"));
   assert(strstr(screen, "\033[H\033[2J"));
   free(screen);

   capture_reset(output);
   tui_window_t *client_log = tui_window_create("client log");
   assert(client_log);
   int client_log_count = client_log->log_count;
   tui_print(client_log, "INACTIVE-LOG-SENTINEL");
   screen = capture_read(output);
   assert(client_log->log_count == client_log_count + 1);
   assert(!strstr(screen, "\033[H\033[2J"));
   assert(!strstr(screen, "INACTIVE-LOG-SENTINEL"));
   free(screen);

   capture_reset(output);
   ui_mode = UI_MODE_TUI;
   unsigned before = redraws;
   state("A", 7100456, "LSB"); // inactive VFO still changes top row immediately
   assert(redraws > before);
   screen = capture_read(output);
   assert(strstr(screen, "\033[1;1H TOPMARK B 7100456"));
   free(screen);

   capture_reset(output);
   dict_add(cfg, "tui.status-line", "NEW ${active_mode}");
   tui_redraw_screen(); // edits/reload picked up without changing render registration
   screen = capture_read(output);
   assert(strstr(screen, "\033[1;1H NEW USB"));
   assert(strstr(screen, "BOTTOM-SENTINEL"));
   free(screen);

   capture_reset(output);
   char long_line[200];
   memset(long_line, 'X', sizeof(long_line) - 1);
   long_line[sizeof(long_line) - 1] = 0;
   dict_add(cfg, "tui.status-line", long_line);
   tui_redraw_screen();
   screen = capture_read(output);
   const char *top = strstr(screen, "\033[1;1H ") + strlen("\033[1;1H ");
   unsigned columns = 0;
   while (top[columns] == 'X') {
      columns++;
   }
   assert(columns == (unsigned)tui_cols() - 1);
   free(screen);

   capture_reset(output);
   tui_fini();
   screen = capture_read(output);
   assert(strstr(screen, "\033[0m"));
   assert(strstr(screen, "\033[?25h"));
   assert(strstr(screen, "\033[?1000l"));
   assert(strstr(screen, "\033[?1006l"));
   assert(strstr(screen, "\033[?2004l"));
   assert(strstr(screen, "\033[r"));
   assert(strstr(screen, "\033[999;1H\033[2K\r\n"));
   free(screen);

   capture_reset(output);
   tui_fini();
   screen = capture_read(output);
   assert(!*screen); // shutdown is safe when cleanup and atexit both invoke it
   free(screen);

   fflush(stdout);
   assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
   close(saved_stdout);
   fclose(output);
   assert(rrclient_room_join("#tab-room"));
   assert(rrclient_room_join("private-user"));
   assert(rrclient_room_join("#removed-room"));
   rrclient_rooms_disconnect();
   rrclient_rooms_disconnect();
   assert(!rrclient_room_iter(0));
   rrclient_rooms_set_available("#rig #tab-room");
   rrclient_rooms_rejoin_available();
   assert(rejoin_count == 2);
   assert((!strcmp(rejoin_targets[0], "#tab-room") && !strcmp(rejoin_targets[1], "#rig")) ||
      (!strcmp(rejoin_targets[1], "#tab-room") && !strcmp(rejoin_targets[0], "#rig")));
   rrclient_rooms_rejoin_available();
   assert(rejoin_count == 2);
   rrclient_rooms_clear();
   puts("PASS: live TUI rows, redraw batching, inactive logs, clipping, and terminal cleanup");
}
