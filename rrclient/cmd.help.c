//
// rrclient/ui.help.c: Core of GTK gui
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>

#define	HELP_DESC_COL 18

extern client_cmd_t client_cmds[];

static bool safe_name(const char *name) {
   // reject empty names or those containing path separators or parent refs
   if (!name || !*name) {
      return false;
   }

   if ( strstr(name, "..") || strchr(name, '/') || strchr(name, '\\') ) {
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
 *        ui_print(NULL, help_main[i]);
 *        i++;
 *     }
 *  } else {
 *     char path[256];
 *     char line[1024];
 *
 *     // Sanitize the user input if ( !safe_name(topic) ) {
 *        ui_print(NULL, "Invalid help topic");
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
 *        ui_print(NULL, "Help file '%s' not found", path);
 *
 *        return;
 *     }
 *     ui_print(NULL, "********************************");
 *     ui_print(NULL, "* HELP for %s", topic);
 *
 *     while ( fgets(line, sizeof(line), fp) ) {
 *        size_t len = strlen(line);
 *
 *        // remove trailing newlines and carriage returns while ( len && (line[len - 1]
 * == '\n' || line[len - 1] == '\r') ) {
 *           line[--len] = '\0';
 *        }
 *        // Present it to the user with ui_print ui_print(NULL, line);
 *     }
 *     fclose(fp);
 *  }
 *  }
 */

////////////////
// Help stuff //
////////////////
const char *help_msg_before[] = {
   "{bright-magenta}******************************************",
   "{bright-magenta}*          rustyrig client help          *",
   "{bright-magenta}******************************************{reset}",
   NULL
};

const char *help_msg_after[] = {
   "\n",
   "{bright-magenta}{underlne}*** Keyboard Shortcuts ***",
   "\t{bright-green}alt-X (1-0)  {bright-yellow}Switch to window 1-10",
   "\t{bright-green}alt-left     {bright-yellow}Switch to previous win",
   "\t{bright-green}alt-right    {bright-yellow}Switch to next win",
   "\t{bright-green}F12          {bright-yellow}PTT toggle{reset}",
   NULL
};


bool cmd_help(int argc, char **args) {
   int i = 0;
   // Pre-message
   while (help_msg_before[i]) {
      ui_print(NULL, help_msg_before[i]);
      i++;
   }
   int longest = 0;

   for (int i = 0 ; client_cmds[i].cmd ; i++) {
      int len = strlen(client_cmds[i].cmd);

      if (len > longest) {
         longest = len;
      }
   }

   int desc_col = 3 + longest + 2;

   for (int i = 0 ; client_cmds[i].cmd ; i++) {
      int len = strlen(client_cmds[i].cmd);
      int spaces = desc_col - 3 - len;

      if (spaces < 1) {
         spaces = 1;
      }

      ui_print(NULL, "\t{bright-green}/%s%*s{bright-yellow}%s{reset}", client_cmds[i].cmd, spaces, "",
         client_cmds[i].desc);
   }

   // after message
   i = 0;
   while (help_msg_after[i]) {
      ui_print(NULL, help_msg_after[i]);
      i++;
   }
   return false;
}
