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
#include <librustyaxe/logger.h>
#include <librrprotocol/rrprotocol.h>
#include <ev.h>
#define	MAX_WINDOWS	32
#define INPUT_HISTORY_MAX 64

extern bool irc_input_cb(const char *input);
extern bool rrcli_autoconnect(void);
extern bool rrcli_connect(const char *url);
extern bool rrcli_disconnect(void);
extern void rrcli_poll_events(void);
#if defined(USE_MONGOOSE)
extern struct mg_mgr mgr;
#endif

struct ev_loop *loop = NULL;
bool dying = false;
bool debug_sockets = false;
bool mirc_colors = true;
time_t now = 0;
static ev_timer tui_clock_watcher;
static ev_timer ws_poll_watcher;

static void ws_poll_cb(EV_P_ ev_timer *w, int revents) {
    (void)w; (void)revents;
#if defined(USE_MONGOOSE)
    rrcli_poll_events();
#endif
}

struct GlobalState rig;

// XXX: These need to be static

const char *configs[] = {
   "~/.config/rrcli.cfg",
   "./config/rrcli.cfg",
};
const int num_configs = sizeof(configs) / sizeof(configs[0]);
extern bool config_network_cb(const char *path, int line, const char *section, const char *buf);

static void tui_stop_clock_timer(struct ev_loop *loop) {
   ev_timer_stop(loop, &tui_clock_watcher);
}

bool rrcli_cleanup(void) {
    logger_end();
    dict_free(cfg);
    tui_stop_clock_timer(loop);
    tui_raw_mode(false);
    exit(0);
    return false;
}

static void tui_clock_cb(EV_P_ ev_timer *w, int revents) {
   (void)w; (void)revents;

   if (dying) {
      rrcli_cleanup();
   }
   tui_redraw_clock();
}

static void tui_start_clock_timer(struct ev_loop *loop) {
   ev_timer_init(&tui_clock_watcher, tui_clock_cb, 0, 1.0); // start after 0s, repeat every 1s

   ev_timer_start(loop, &tui_clock_watcher);
}


static void rrcli_handle_log_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
    (void)event;
    (void)cptr;
    (void)user;
    struct log_event_data *led = (struct log_event_data *)data;
    if (!led || !led->message[0]) {
       return;
    }
    tui_window_t *status = tui_window_find("status");
    if (status) {
       tui_print_win(status, "%s", led->message);
    }
}

struct talk_msg_event_data {
    char from[128];
    char data[4096];
    char target[128];
    char msg_type[32];
    time_t ts;
};

static void rrcli_handle_talk_msg_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
    (void)event;
    (void)cptr;
    (void)user;
    struct talk_msg_event_data *tmed = (struct talk_msg_event_data *)data;
    if (!tmed || !tmed->from[0] || !tmed->data[0]) {
       return;
    }

    tui_window_t *wp = tui_active_window();
    if (strcasecmp(tmed->msg_type, "action") == 0) {
       tui_print_win(wp, "%s * %s %s", get_chat_ts(tmed->ts), tmed->from, tmed->data);
    } else {
       tui_print_win(wp, "%s {bright-black}<{bright-cyan}%s{bright-black}>{reset} %s{reset}", get_chat_ts(tmed->ts), tmed->from, tmed->data);
    }
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
   event_on("log.message", rrcli_handle_log_event, NULL);
   event_on("talk.msg", rrcli_handle_talk_msg_event, NULL);

   // Setup stdio & clock
   tui_start_clock_timer(loop);

#if defined(USE_MONGOOSE)
   mg_mgr_init(&mgr);
   ev_timer_init(&ws_poll_watcher, ws_poll_cb, 0, 0.05);
   ev_timer_start(loop, &ws_poll_watcher);
   rrcli_autoconnect();
#endif
   ev_run(loop, 0);

   /////////////
   // cleanup //
   /////////////
   rrcli_cleanup();

   return 0;
}
