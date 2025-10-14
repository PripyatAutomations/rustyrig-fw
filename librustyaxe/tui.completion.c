// tui.completion.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>

/*
completion:
  - This needs to become aware of the focused window
   if line starts with / and no space: cli_command_t.cmd
   if word starts with & or #, check channels joined
   Else, show users in current channel if a channel window
 */  

char *tui_completion_generator(const char* text, int state) {
   static int list_index, len;

   // XXX: replace this with nicks/channels available
   // XXX: Use the active window's userlist (if a channel target)
   static const char* my_options[] = {
//        "apple", "banana", "grape", "orange", "peach", NULL
      NULL
   };

   if (state == 0) {
      list_index = 0;
      len = strlen(text);
   }

   while (my_options[list_index]) {
      const char* name = my_options[list_index++];
      if (strncmp(name, text, len) == 0) {
         return strdup(name);  // Return a copy for readline to use
      }
   }
   return NULL; // No more matches
}

// This is the function readline calls for completion
char **tui_completion_cb(const char* text, int start, int end) {
//   rl_attempted_completion_over = 1;
//   return rl_completion_matches(text, tui_completion_generator);
   return NULL;
}
