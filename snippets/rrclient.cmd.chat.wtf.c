bool cmd_notice(int argc, char **args) {
   if (argc < 1) {
      // XXX: cry not enough args
      return true;
   }

   dict *d = json2dict(args[1]);

   if (ui_mode == UI_MODE_TUI) {
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
         memset( fullmsg, 0, sizeof(fullmsg) );
         size_t pos = 0;

         for (int i = 2 ; i < argc ; i++) {
            int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");

            if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
               break;
            }
            pos += n;
         }

         ui_print(NULL, "-> *%s* %s", target, fullmsg);
      }
   } else {
      ui_print(NULL, "{yellow}NOTICE{reset}: {bright-cyan}%s{reset}: %s", target, fullmsg);
   return false;
}
