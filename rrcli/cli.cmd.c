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
#include <librrprotocol/rrprotocol.h>
#include <ev.h>
#if defined(USE_MONGOOSE)
extern struct mg_connection *ws_conn;
extern bool ws_connected;
extern bool rrcli_send_chat(const char *data);
#endif
extern bool dying;
extern time_t now;

typedef struct cli_command {
   char *cmd;
   char *desc;
   bool (*cb)(int argc, char **args);
   event_cb_t (*event_cb)(const char *event, void *data, irc_conn_t *cptr, void *user);
} cli_command_t;



bool cli_join(int argc, char **args) {
    (void)argc; (void)args;
    tui_print_win(tui_active_window(), "{yellow}JOIN is not supported over WebSocket{reset}");
    return false;
}

bool cli_me(int argc, char **args) {
    char buf[1024];
    memset(buf, 0, 1024);
    size_t pos = 0;
    for (int i = 1; i < argc; i++) {
       int n = snprintf(buf + pos, sizeof(buf) - pos, "%s%s", (i > 1 ? " " : ""), args[i] ? args[i] : "");
       if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
          break;
       }
       pos += n;
    }
    const char *jp = dict2json_mkstr(
       VAL_STR, "talk.cmd", "msg",
       VAL_STR, "talk.data", buf,
       VAL_STR, "talk.msg_type", "action");
#if defined(USE_MONGOOSE)
    if (ws_conn) {
       mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
    }
#endif
    free((void *)jp);
    return false;
}

bool cli_msg(int argc, char **args) {
    if (argc < 2) {
       return true;
    }

    char *target = args[1];
    char fullmsg[502];
    memset(fullmsg, 0, sizeof(fullmsg));
    size_t pos = 0;

    for (int i = 2; i < argc; i++) {
       int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");
       if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
          break;
       }
       pos += n;
    }

    tui_window_t *wp = tui_active_window();
    tui_print_win(wp, "-> %s %s", target, fullmsg);

    const char *jp = dict2json_mkstr(
       VAL_STR, "talk.cmd", "msg",
       VAL_STR, "talk.data", fullmsg,
       VAL_STR, "talk.target", target);
#if defined(USE_MONGOOSE)
    if (ws_conn) {
       mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
    }
#endif
    free((void *)jp);
    return false;
}

bool cli_notice(int argc, char **args) {
   if (argc < 2) {
      // XXX: cry not enough args
      return true;
   }

   tui_window_t *wp = NULL;
   bool new_win = false;

   if (*args[1]) {
      wp = tui_window_find(args[1]);
      if (!wp) {
         new_win = true;
         wp = tui_window_create(args[1]);
         wp->cptr = tui_active_window()->cptr;
      }
   }
   
   if (!wp) {
      wp = tui_active_window();
   }

   // There's a window here at least...
   if (wp->cptr) {
      char *target = wp->title;

      if (args[1]) {
         target = args[1];
      }

      char fullmsg[502];
      memset(fullmsg, 0, sizeof(fullmsg));
      size_t pos = 0;

      for (int i = 2; i < argc; i++) {
         int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");
         if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
            break;
         }
         pos += n;
      }
       tui_print_win(wp, "-> *%s* %s", target, fullmsg);
       tui_print_win(wp, "{yellow}NOTICE is not supported over WebSocket{reset}");
   }
   return false;
}

bool cli_part(int argc, char **args) {
    (void)argc; (void)args;
    tui_print_win(tui_active_window(), "{yellow}PART is not supported over WebSocket{reset}");
    return false;
}

bool cli_quit(int argc, char **args) {
    (void)argc; (void)args;
    tui_window_t *wp = tui_active_window();
    tui_print_win(wp, "Goodbye!");
    dying = true;
    return false;
}

bool cli_quote(int argc, char **args) {
   if (argc < 1) {
      // XXX: cry not enough args
      return true;
   }

   tui_window_t *wp = tui_active_window();
   char fullmsg[502];
   memset(fullmsg, 0, sizeof(fullmsg));
   size_t pos = 0;

   for (int i = 1; i < argc; i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 1 ? " " : ""), args[i] ? args[i] : "");
      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }
    tui_print_win(wp, "-raw-> %s", fullmsg);
    tui_print_win(wp, "{yellow}QUOTE is not supported over WebSocket{reset}");
    return false;
}

bool cli_topic(int argc, char **args) {
    (void)argc; (void)args;
    tui_print_win(tui_active_window(), "{yellow}TOPIC is not supported over WebSocket{reset}");
    return false;
}

bool cli_whois(int argc, char **args) {
    (void)argc; (void)args;
    tui_print_win(tui_active_window(), "{yellow}WHOIS is not supported over WebSocket{reset}");
    return false;
}

bool cli_win(int argc, char **args) {
   if (argc < 1) {
      return true;
   }

   if (strcasecmp(args[1], "close") == 0) {
      Log(LOG_CRIT, "test", "argc: %d args0: %s args1: %s", argc, args[0], args[1]);
      if (argc < 2) {
         return true;
      }

      int id = -1;
      if (argc >= 3) {
         id = atoi(args[2]);
      } else {
         return tui_window_destroy(tui_active_window());
      }

      if (id > 0) {
         tui_window_destroy_id(id);
         return false;
      }
      return true;
   }

   int id = atoi(args[1]);
//   tui_print_win(tui_active_window(), "ID: %s", args[1]);

   if (id < 1 || id > TUI_MAX_WINDOWS) {
      tui_print_win(tui_active_window(), "Invalid window %d, must be between 1 and %d", id, TUI_MAX_WINDOWS);
      return true;
   }

   tui_window_focus_id(id);
   return false;
}

bool cli_clear(int argc, char **args) {
   tui_clear_scrollback(tui_active_window());
   return false;
}
extern bool cli_help(int argc, char **args);
cli_command_t cli_commands[] = {
    { .cmd = "/clear",  .cb = cli_clear,  .desc = "Clear the scrollback" },
    { .cmd = "/help",   .cb = cli_help,   .desc = "Show help message" },
    { .cmd = "/join",   .cb = cli_join,   .desc = "Join a channel (N/A over WS)" },
    { .cmd = "/me",     .cb = cli_me,     .desc = "\tSend an action to the current channel" },
    { .cmd = "/msg",    .cb = cli_msg,    .desc = "Send a private message" },
    { .cmd = "/notice", .cb = cli_notice, .desc = "Send a private notice (N/A over WS)" },
    { .cmd = "/part",   .cb = cli_part,   .desc = "leave a channel (N/A over WS)" },
    { .cmd = "/quit",   .cb = cli_quit,   .desc = "Exit the program" },
    { .cmd = "/quote",  .cb = cli_quote,  .desc = "Send a raw command (N/A over WS)" },
    { .cmd = "/topic",  .cb = cli_topic,  .desc = "Set channel topic (N/A over WS)" },
    { .cmd = "/win",    .cb = cli_win,    .desc = "Change windows" },
    { .cmd = "/whois",  .cb = cli_whois,  .desc = "Show client information (N/A over WS)" },
    { .cmd = NULL,      .cb = NULL,       .desc = NULL }
};

bool cli_help(int argc, char **args) {
   tui_window_t *wp = tui_active_window();
   if (!wp) {
      return true;
   }

   tui_print_win(wp, "*** Available commands ***");
   for (int i = 0; cli_commands[i].cmd; i++) {
      tui_print_win(wp, "   %s\t\t%s", cli_commands[i].cmd, cli_commands[i].desc);
   }
   tui_print_win(wp, ""); 
   tui_print_win(wp, "*** Keyboard Shortcuts ***"); 
   tui_print_win(wp, "   alt-X (1-0)\t\tSwitch to window 1-10");
   tui_print_win(wp, "   alt-left\t\tSwitch to previous win");
   tui_print_win(wp, "   alt-right\t\tSwitch to next win");
   tui_print_win(wp, "   F12\t\t\tPTT toggle");
   return false;
}

bool irc_input_cb(const char *input) {
   if (!cli_commands || !input || !*input) {
      return true;
   }

   // Make a mutable copy
   char buf[TUI_INPUTLEN];
   strncpy(buf, input, sizeof(buf) - 1);
   buf[sizeof(buf) - 1] = '\0';

   // Tokenize into argc/args
   int argc = 0;
   char *args[64];   // max 64 tokens
   char *tok = strtok(buf, " \t");
   while (tok && argc < (int)(sizeof(args) / sizeof(args[0]))) {
      args[argc++] = tok;
      tok = strtok(NULL, " \t");
   }

   if (argc == 0) {
      return true;
   }

   if (args[0][0] == '/') {
      for (cli_command_t *c = cli_commands; c->cmd && c->cb; c++) {
         if (strcasecmp(c->cmd, args[0]) == 0) {
            if (c->cb) {
               c->cb(argc, args);
               return false;
            }
         }
      }

      tui_print_win(tui_active_window(), "{red}*** {bright-red}Huh?! What you say?! I dont understand '%s' {red}***{reset}.", args[0]);
      return true;
   }

     // Send to active window target
     tui_window_t *wp = tui_active_window();
     if (wp) {
        if (strcasecmp(wp->title, "status") == 0) {
           tui_print_win(tui_active_window(), "{red}*** {bright-red}Huh? What you say??? {red}***{reset}.");
        } else {
#if defined(USE_MONGOOSE)
           if (!ws_connected) {
              tui_print_win(wp, "{red}*** Not connected to server ***{reset}");
              return false;
           }
#endif
           char fullmsg[502];
           memset(fullmsg, 0, sizeof(fullmsg));
           size_t pos = 0;
           for (int i = 0; i < argc; i++) {
              int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 0 ? " " : ""), args[i] ? args[i] : "");
              if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
                 break;
              }
              pos += n;
           }
           rrcli_send_chat(fullmsg);
        }
     }

   return false;
}
