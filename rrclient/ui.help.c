//
// rrclient/ui.help.c: Core of GTK gui
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
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
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "../ext/libmongoose/mongoose.h"
#include <librustyaxe/core.h>
#include "rrclient/auth.h"
#include "mod.ui.gtk3/gtk.core.h"
#include "rrclient/ws.h"


static bool safe_name(const char *name) {
   // reject empty names or those containing path separators or parent refs
   if (!name || !*name) {
      return false;
   }

   if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) {
      return false;
   }
   return true;
}

char *help_main[] = {
     "******************************************",
     "          rustyrig client help           *",
     "******************************************",
     "",
     "[Server Commands]",
     "   /server [name]          Connect to a server (or default)",
     "   /disconnect             Disconnect from server",
     "[Chat Commands]",
     "   /clear                  Clear chat tab",
     "   /clearlog               Clear the syslog tab",
     "   /die                    Shut down server",
//     "   /edit                   Edit a user",
     "   /help                   This help text",
     "   /kick [user] [reason]   Kick the user",
     "   /me [message]           Send an ACTION",
     "   /mute [user] [reason]   Disable controls for user",
     "   /names                  Show who's in the chat",
     "   /restart [reason]       Restart the server",
//     "   /rxmute                  MUTE RX audio",
     "   /rxvol [vol;ume]        Set RX volume",
//     "   /rxunmute               Unmute RX audio",
//     "   /txvol [val]            Set TX gain",
     "   /unmute [user]          Unmute user",
     "   /whois [user]           WHOIS a user",
     "   /quit [reason]          End the session",
     "",
     "[UI Commands]",
     "   /chat                   Switch to chat tab",
     "   /config | /cfg          Switch to config tab",
     "   /log | /syslog          Switch to syslog tab",
     "",
     " See /help keybindings or alt-h for keyboard help!",
     "******************************************",
     NULL
};

void show_help(const char *topic) {
  if (!topic) {
     int i = 0;
     while (help_main[i]) {
        ui_print(help_main[i]);
        i++;
     }
  } else {
     char path[256];
     char line[1024];

     // Sanitize the user input
     if (!safe_name(topic)) {
        ui_print("Invalid help topic");
        return;
     }

     // Find the help file
     const char *help_dir = cfg_get_exp("path.help-dir");
     // did we get a key from the cfgstore?
     if (help_dir) {
        snprintf(path, sizeof(path), "%s/%s", help_dir, topic);
        free((void *)help_dir);
     } else {
        snprintf(path, sizeof(path), "./help/%s", topic);
     }

     FILE *fp = fopen(path, "r");
     if (!fp) {
        ui_print("Help file '%s' not found", path);
        return;
     }
     ui_print("********************************");
     ui_print("* HELP for %s", topic);

     while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);

        // remove trailing newlines and carriage returns
        while (len && (line[len-1] == '\n' || line[len-1] == '\r')) {
           line[--len] = '\0';
        }
        // Present it to the user with ui_print
        ui_print(line);
     }
     fclose(fp);
  }
}
