//
// rrclient/cmd.help.c: help stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>

#define	HELP_DESC_COL 18

extern client_cmd_t client_cmds[];

struct help_line {
   enum GuiMode mode;
   const char *line;
};
typedef struct help_line help_line_t;

static bool safe_name(const char *name) {
   // reject empty names or those containing path separators or parent refs
   if (!name || !*name) {
      return false;
   }

   if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\') ) {
      return false;
   }

   return true;
}

// This needs removed asap, its dead code
/*
 *  void gui_show_help(const char *topic) {
 *  if (!topic) {
 *     int i = 0;
 *     while (help_main[i]) {
 *        ui_print(ui_active_window_name(), help_main[i]);
 *        i++;
 *     }
 *  } else {
 *     char path[256];
 *     char line[1024];
 *
 *     // Sanitize the user input if ( !safe_name(topic) ) {
 *        ui_print(ui_active_window_name(), "Invalid help topic");
 *
 *        return;
 *     }
 *     // Find the help file const char *help_dir = cfg_get_exp("path.help-dir");
 *
 *     // did we get a key from the cfgstore?
 *     if (help_dir) {
 *        snprintf(path, sizeof(path), "%s/%s", help_dir, topic);
 *        free( (void *)help_dir );
 *     } else {
 *        snprintf(path, sizeof(path), "./help/%s", topic);
 *     }
 *     FILE *fp = fopen(path, "r");
 *
 *     if (!fp) {
 *        ui_print(ui_active_window_name(), "Help file '%s' not found", path);
 *
 *        return;
 *     }
 *     ui_print(ui_active_window_name(), "********************************");
 *     ui_print(ui_active_window_name(), "* HELP for %s", topic);
 *
 *     while ( fgets(line, sizeof(line), fp) ) {
 *        size_t len = strlen(line);
 *
 *        // remove trailing newlines and carriage returns while ( len && (line[len - 1]
 * == '\n' || line[len - 1] == '\r') ) {
 *           line[--len] = '\0';
 *        }
 *        // Present it to the user with ui_print ui_print(ui_active_window_name(), line);
 *     }
 *     fclose(fp);
 *  }
 *  }
 */


////////////////
// Help stuff //
////////////////
static help_line_t help_msg_before[] = {
   {
      UI_MODE_NONE, "\00304******************************************"
   },
   {
      UI_MODE_NONE, "\00304*          rustyrig client help          *"
   },
   {
      UI_MODE_NONE, "\00304******************************************\017"
   },
   {
      UI_MODE_NONE, NULL
   }
};

static help_line_t help_msg_after[] = {
   { UI_MODE_NONE, "\t\00309/rxcodec /txcodec \00308Shared codec changes require account RX/TX privilege for that VFO" },
   { UI_MODE_NONE, "\t\00309/room remove #room [token] \00308Admin/owner: hide room, preserving its data; confirm with server token" },
   { UI_MODE_NONE, "\t\00309/room remove #room -f [-h] [token] \00308Delete room record/bindings; -h also deletes chat history" },
   { UI_MODE_NONE, "\t\00309/room add #room \00308Anyone: create undashed room; admin/owner: dashed rooms or restoration; rig logs stay" },
   {
      UI_MODE_NONE, ""
   },
   {
      UI_MODE_NONE, "\t\00304*** \037Server rigctl commands\037 ***"
   },
   {
      UI_MODE_NONE, "\t\00309!help        \00308Show the server side rigctl help"
   },
   {
      UI_MODE_NONE, "\t\00309!freq <freq> \00308Set frequency - 7200, 7.2m or 7200000 form"
   },
   {
      UI_MODE_NONE, "\t\00309!mode <mode> \00308Set mode to CW|AM|LSB|USB|FM|D-L|D-U"
   },
   {
      UI_MODE_NONE, "\t\00309!mode lsb freq 7200 \00308Chain rig commands on one line"
   },
   {
      UI_MODE_NONE, "\t\00309!power <watts>\00308 Set power in watts (e.g. !power 25)"
   },
   {
      UI_MODE_NONE, "\t\00309!width <w>   \00308Set passband width (narrow|normal|wide)"
   },
   {
      UI_MODE_NONE, "\t\00309!vfo <vfo>   \00308Switch VFOs (A|B|C)"
   },
   {
      UI_MODE_NONE, ""
   },
   {
      UI_MODE_NONE, "\t\00304*** \037Keyboard Shortcuts\037 ***"
   },
   {
      UI_MODE_GTK, "\t\00309Ctrl +/-     \00308Zoom fonts and resize window; Alt +/- changes fonts only; Ctrl/Alt 0 resets zoom"
   },
   {
      UI_MODE_GTK, "\t\00309Touch         \00308Pinch zooms fonts only; two-finger tap opens a userlist menu"
   },
   {
      UI_MODE_GTK, "\t\00309alt-c         \00308Focus chat input"
   },
   {
      UI_MODE_NONE, "\t\00309alt-# (1-0)   \00308Switch to window 1-10"
   },
   {
      UI_MODE_NONE, "\t\00309esc-# (1-0)   \00308Switch to window 1-10"
   },
   {
      UI_MODE_NONE, "\t\00309alt-enter     \00308Toggle PTT"
   },
   {
      UI_MODE_NONE, "\t\00309ctrl-space    \00308Toggle PTT"
   },
   {
      UI_MODE_NONE, "\t\00309alt-left      \00308Switch to previous win"
   },
   {
      UI_MODE_NONE, "\t\00309alt-right     \00308Switch to next win"
   },
   {
      UI_MODE_GTK, "\t\00309Mode first letter \00308Cycle matching modes in the mode selector"
   },
   {
      UI_MODE_GTK, "\t\00309Width A/N/W   \00308Select narrow/normal/wide in the width selector"
   },
   {
      UI_MODE_GTK, "\t\00309F11           \00308Fullscreen toggle\017"
   },
   {
      UI_MODE_NONE, NULL
   }
};

bool cmd_help(int argc, char **args) {
   // TUI: defer the full-screen redraw until all lines are printed, otherwise
   // each line causes a full redraw and this takes forever
   bool deferred = false;

   if (ui_mode == UI_MODE_TUI) {
      tui_redraw_defer();
      deferred = true;
   }

   // Pre-message
   for (int i = 0 ; help_msg_before[i].line ; i++) {
      if (help_msg_before[i].mode == UI_MODE_NONE ||
          ui_mode == help_msg_before[i].mode) {
         ui_print(ui_active_window_name(), help_msg_before[i].line);
      }
   }

   int longest = 0;

   for (int i = 0 ; client_cmds[i].cmd ; i++) {
      int len = strlen(client_cmds[i].cmd);

      if (len > longest) {
         longest = len;
      }
   }

   int desc_col = 3 + longest + 2;

   const char *section = NULL;

   /* PARITY: rustyrig-www/js/webui.chat.js:webui_command_help */
   for (int i = 0 ; client_cmds[i].cmd ; i++) {
      // Hide admin-only commands from non-staff users
      if ( client_cmds[i].admin && !media_have_priv("admin|owner") ) {
         continue;
      }
      const char *next_section = client_cmds[i].help_section ? client_cmds[i].help_section : "Other";
      if (!section || strcmp(section, next_section)) {
         ui_print(ui_active_window_name(), "\00304%s\017", next_section);
         section = next_section;
      }
      int len = strlen(client_cmds[i].cmd);
      int spaces = desc_col - 3 - len;

      if (spaces < 1) {
         spaces = 1;
      }

      ui_print(ui_active_window_name(), "\t\00309/%s%*s\00308%s\017", client_cmds[i].cmd, spaces,
         "", client_cmds[i].desc);
   }

   // After-message
   for (int i = 0 ; help_msg_after[i].line ; i++) {
      if (help_msg_after[i].mode == UI_MODE_NONE ||
          ui_mode == help_msg_after[i].mode) {
         ui_print(ui_active_window_name(), help_msg_after[i].line);
      }
   }

   if (deferred) {
      tui_redraw_flush();
   }

   return false;
}
