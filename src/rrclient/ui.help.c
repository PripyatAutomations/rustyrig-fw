//
// rrclient/ui.help.c: Core of GTK gui
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
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
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
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

void show_help(const char *topic) {
  if (!topic) {
     ui_print("******************************************");
     ui_print("          rustyrig v.%s help", VERSION);
     ui_print("[Server Commands]");
     ui_print("   /server [name]          Connect to a server (or default)");
     ui_print("   /disconnect             Disconnect from server");
     ui_print("[Chat Commands]");
     ui_print("   /clear                  Clear chat tab");
     ui_print("   /clearlog               Clear the syslog tab");
     ui_print("   /die                    Shut down server");
  //   ui_print("   /edit                   Edit a user");
     ui_print("   /help                   This help text");
     ui_print("   /kick [user] [reason]   Kick the user");
     ui_print("   /me [message]           Send an ACTION");
     ui_print("   /mute [user] [reason]   Disable controls for user");
     ui_print("   /names                  Show who's in the chat");
     ui_print("   /restart [reason]       Restart the server");
  //   ui_print("   /rxmute                  MUTE RX audio");
     ui_print("   /rxvol [vol;ume]        Set RX volume");
     ui_print("   /rxunmute               Unmute RX audio");
  //   ui_print("   /txmute                 Mute TX audio");
  //   ui_print("   /txvol [val]            Set TX gain");
     ui_print("   /unmute [user]          Unmute user");
     ui_print("   /whois [user]           WHOIS a user");
     ui_print("   /quit [reason]          End the session");
     ui_print("");
     ui_print("[UI Commands]");
     ui_print("   /chat                   Switch to chat tab");
     ui_print("   /config | /cfg          Switch to config tab");
     ui_print("   /log | /syslog          Switch to syslog tab");
     ui_print("[Key Combinations");
     ui_print("   alt-1 thru alt-3        Change tabs in main window");
     ui_print("   alt-u                   Toggle userlist");
     ui_print("******************************************");
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
