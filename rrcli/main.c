//
// a simple TUI client for the remote station, using minimal external libraries
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <ev.h>
#define	MAX_WINDOWS	32
#define INPUT_HISTORY_MAX 64

extern bool irc_input_cb(const char *input);
extern bool autoconnect(void);
extern void rr_set_irc_conn_pool(void);
extern void on_privmsg(const char *event, void *data, irc_conn_t *cptr, void *user);

struct ev_loop *loop = NULL;
bool dying = false;
bool debug_sockets = false;
bool mirc_colors = true;
time_t now = 0;
static ev_timer tui_clock_watcher;

// XXX: These need to be static

const char *configs[] = {
   "~/.config/rrcli.cfg",
   "./config/rrcli.cfg",
};
const int num_configs = sizeof(configs) / sizeof(configs[0]);
extern bool config_network_cb(const char *path, int line, const char *section, const char *buf);

static void tui_clock_cb(EV_P_ ev_timer *w, int revents) {
   (void)w; (void)revents;
   tui_redraw_clock();
}

static void tui_start_clock_timer(struct ev_loop *loop) {
   ev_timer_init(&tui_clock_watcher, tui_clock_cb, 0, 1.0); // start after 0s, repeat every 1s
   ev_timer_start(loop, &tui_clock_watcher);
}

static void tui_stop_clock_timer(struct ev_loop *loop) {
   ev_timer_stop(loop, &tui_clock_watcher);
}

bool rrcli_cleanup(void) {
   logger_end();
   dict_free(cfg);
   tui_stop_clock_timer(loop);
   tui_raw_mode(false);
   return false;
}

/////////////////
// local hooks //
/////////////////

int main(int argc, char **args) {
   now = time(NULL);
   loop = EV_DEFAULT;
   char *fullpath = NULL;

   event_init();
   tui_readline_cb = irc_input_cb;	// set our input callback
   tui_init();
   tui_print_win(tui_window_find("status"), "rrcli starting");

   // add our configuration callbacks
   cfg_add_callback(NULL, "network:*", config_network_cb);

   if ((fullpath = find_file_by_list(configs, num_configs))) {
      if (fullpath && !(cfg = cfg_load(fullpath))) {
         tui_print_win(tui_window_find("status"), "Couldn't load config \"%s\", using defaults instead", fullpath);
      }
      free(fullpath);
   }

   // apply some global configuration
   const char *logfile = cfg_get_exp("log.file");
   logger_init((logfile ? logfile : "rrcli.log"));
   free((char *)logfile);
   debug_sockets = cfg_get_bool("debug.sockets", false);

   // Setup stdio & clock
   tui_start_clock_timer(loop);

   // XXX: this needs moved to module_init in mod.proto.irc
   irc_init();
   mirc_colors = cfg_get_bool("irc.colors", false);
   event_on("irc.privmsg", on_privmsg, NULL);
   rr_set_irc_conn_pool();
   autoconnect();

   ev_run(loop, 0);

   /////////////
   // cleanup //
   /////////////
   rrcli_cleanup();

   return 0;
}
