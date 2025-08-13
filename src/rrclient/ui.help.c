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
     // Sanitize the user input
     // Find the help/ file
     // Present it to the user with ui_print
  }
}
