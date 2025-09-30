//
// irc.parser.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
//#include "build_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>

static char *log_buffer[LOG_LINES];
static int log_head = 0;
static char status_line[STATUS_LEN] = "Status: starting...";
static int term_rows = 24;;  // default lines
static int term_cols = 80;  // default width

static void update_term_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
       term_rows = ws.ws_row;
       term_cols = ws.ws_col;
       fprintf(stderr, "tui", "tty sz: %d x %d", term_cols, term_rows);
    } else {
       fprintf(stderr, "failed TIOCGWINSZ %d: %s\n", errno, strerror(errno));
    }
}

static int term_width(void) {
    return term_cols;
}

static void clear_line(int row) {
    printf("\033[%d;1H\033[2K", row);  // move to row, clear entire line
}

void redraw_screen(void) {
    update_term_size();

    // Clear entire screen
    printf("\033[H\033[2J");

    // Calculate how many log lines we can print
    int log_lines_to_print = term_rows - STATUS_LINES - 1;
    if (log_lines_to_print < 0) {
        log_lines_to_print = 0;
    }
    if (log_lines_to_print > LOG_LINES) {
        log_lines_to_print = LOG_LINES;
    }

    // Print log lines
    for (int i = 0; i < log_lines_to_print; i++) {
        int idx = (log_head + i) % LOG_LINES;
        if (log_buffer[idx]) {
            printf("%s\n", log_buffer[idx]);
        } else {
            printf("\n");
        }
    }

    // Print full-width status line on second-last row
    printf("\033[%d;1H", term_rows - 1);
    int width = term_cols;
    char buf[width + 1];
    snprintf(buf, sizeof(buf), "%-*.*s", width, width, status_line);
    printf("\033[7m%s\033[0m", buf);

    // Move to bottom row for prompt
    printf("\033[%d;1H", term_rows);

    // Reset readline prompt cleanly
    rl_on_new_line();
    rl_set_prompt("");
    rl_replace_line("", 0);
    printf("\033[%d;1H\033[K", term_rows);

    rl_redisplay();
    fflush(stdout);
}

void add_log(const char *msg) {
    if (log_buffer[log_head]) {
        free(log_buffer[log_head]);
    }
    log_buffer[log_head] = strdup(msg);
    log_head = (log_head + 1) % LOG_LINES;
    redraw_screen();
}

bool update_status(const char *status) {
    if (status) {
        strncpy(status_line, status, sizeof(status_line) - 1);
        status_line[sizeof(status_line) - 1] = '\0';
    }
    redraw_screen();
    return false;
}

static void sigwinch_handler(int signum) {
    (void)signum;
    update_term_size();
    redraw_screen();
}

bool tui_init(void) {
    signal(SIGWINCH, sigwinch_handler);
    update_term_size();
    return false;
}
