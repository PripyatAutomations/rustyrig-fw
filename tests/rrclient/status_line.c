#include <assert.h>
#include "rrclient/vfo.c"
#include "rrclient/ui.statusbar.c"

bool dying, restarting;
time_t now;
enum GuiMode ui_mode = UI_MODE_NONE;
const char *login_user = "operator";
char sb_online[128], sb_window[128], sb_vfo[32];
void tui_refresh_sb_window(void) {}
void tui_refresh_sb_vfo(void) {}
void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {}
const char *rrclient_media_current_codec(bool tx) { return tx ? NULL : "opuT"; }

static unsigned redraws;
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
   if (!line || strcmp(line, expected)) fprintf(stderr, "template: %s\nexpected: %s\nactual: %s\n", format, expected, line ? line : "(null)");
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
int main(void) {
   cfg = dict_new();
   default_cfg = dict_new();
   dict_add(default_cfg, "tui.status-line", RRCLIENT_DEFAULT_STATUS_LINE);
   server_name = "station";
   ws_connected = 1;
   cfg_tui_colors = false;
   tui_window_t window = {0};
   strcpy(window.title, "chat");
   strcpy(window.status_line, "Test topic");
   char *initial = rrclient_tui_topline(&window);
   assert(!strcmp(initial, "Test topic | VFO A: --- kHz ---"));
   free(initial);
   state("A", 7200123, "LSB");
   state("B", 14250000, "USB");
   check(&window, "${active_vfo} ${vfo_a_freq} ${vfo_a_freq_hz} ${vfo_a_freq_khz} ${vfo_a_freq_mhz}",
      "A 7200123 7200123 7200.123 7.200123");
   check(&window, "${vfo_B_mode} ${active_mode} ${active_width} ${active_power} ${active_ptt}",
      "USB LSB 2700 25 RX");
   vfo_state_set_active("B");
   check(&window, "${active_vfo}/${active_freq}/${active_mode}", "B/14250000/USB");
   check(&window, "${window}/${topic}/${server}/${user}/${connection}/${rxcodec}/${txcodec}",
      "chat/Test topic/station/operator/ONLINE/opuT/NONE");
   check(&window, "${missing} ${vfo_z_freq:unknown} ${vfo_z_mode:---} 100%", " unknown --- 100%");
   check(&window, "broken ${active_vfo", "broken ${active_vfo");
   check(&window, "", "");
   cfg_tui_colors = true;
   check(&window, "{red}${active_vfo}{reset}", "\033[31mB\033[0m");
   cfg_tui_colors = false;

   // Exercise the real redraw path and prove the bottom status is independent.
   int saved_stdout = dup(STDOUT_FILENO);
   FILE *output = tmpfile();
   assert(output && dup2(fileno(output), STDOUT_FILENO) >= 0);
   setvbuf(output, NULL, _IONBF, 0);
   tui_window_init();
   tui_set_topline_renderer(render);
   dict_add(cfg, "tui.status-line", "TOPMARK ${active_vfo} ${vfo_a_freq}");
   tui_update_status(tui_active_window(), "BOTTOM-SENTINEL");
   char *screen = capture_read(output);
   assert(strstr(screen, "\033[1;1H TOPMARK B 7200123"));
   const char *bottom = strstr(screen, "\033[23;1H");
   assert(bottom && strstr(bottom, "BOTTOM-SENTINEL"));
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
   while (top[columns] == 'X') columns++;
   assert(columns == (unsigned)tui_cols() - 1);
   free(screen);
   fflush(stdout);
   assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
   close(saved_stdout);
   fclose(output);
   puts("PASS: live top-row templates, inactive VFO updates, colors, fallback, clipping; bottom status preserved");
}
