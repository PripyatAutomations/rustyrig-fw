// Application-owned values for the TUI's configurable top/topic row.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/ui.statusbar.h>
#include <rrclient/vfo.h>
#include <rrclient/media.h>
#include <rrclient/userlist.h>
#include <rrclient/rooms.h>

extern const char *login_user;

static const char *topline_value(const char *name, tui_window_t *win,
   char *value, size_t size) {
   if (!strcmp(name, "active_vfo")) {
      snprintf(value, size, "%c", vfo_state_get_active());
      return value;
   }
   if (!strcmp(name, "window") || !strcmp(name, "win.title")) return win ? win->title : NULL;
   if (!strcmp(name, "topic")) return win ? win->status_line : NULL;
   if (!strcmp(name, "server")) return server_name;
   if (!strcmp(name, "user")) return login_user;
   if (!strcmp(name, "connection")) {
      return ws_connected == 1 ? "ONLINE" : (ws_connected == -1 ? "CONNECTING" : "OFFLINE");
   }
   if (!strcmp(name, "ptt-state")) {
      if (ws_connected != 1) return "{bright-yellow}PTT: WAIT{reset}";
      const char *tx_user = NULL;
      for (struct rr_user *u = global_userlist; u; u = u->next) {
         if (u->room[0] && strcasecmp(u->room, ws_authoritative_room()) != 0) continue;
         if (u->is_ptt) {
            tx_user = u->name;
            break;
         }
      }
      const char active = vfo_state_get_active();
      const bool ptt = vfo_state_get_bool((char[]){ active, 0 }, "cat.state.ptt", false);
      if (ptt && !tx_user) tx_user = login_user ? login_user : "TX";
      if (tx_user) {
         snprintf(value, size, "{bright-red}PTT: %s{reset}", tx_user);
         return value;
      }
      return "{bright-green}PTT: OFF{reset}";
   }
   if (!strcmp(name, "rxcodec") || !strcmp(name, "txcodec")) {
      const char *codec = rrclient_media_current_codec(name[0] == 't');
      return codec ? codec : "NONE";
   }
   if (!strcmp(name, "win.scroll")) {
      snprintf(value, size, "%d", win ? win->scroll_offset : 0);
      return value;
   }

   char vfo[2] = { vfo_state_get_active(), 0 };
   const char *field;
   if (!strncmp(name, "active_", 7)) {
      field = name + 7;
   } else if (!strncmp(name, "vfo_", 4) && strlen(name) > 6 && name[5] == '_' &&
              ((name[4] >= 'a' && name[4] <= 'z') || (name[4] >= 'A' && name[4] <= 'Z'))) {
      vfo[0] = (char)toupper((unsigned char)name[4]);
      field = name + 6;
   } else {
      return NULL;
   }
   if (!strcmp(field, "mode")) return vfo_state_get(vfo, "cat.state.mode", NULL);
   if (!strcmp(field, "ptt")) {
      // Different defaults distinguish a missing value from a reported RX.
      if (vfo_state_get_bool(vfo, "cat.state.ptt", false) !=
          vfo_state_get_bool(vfo, "cat.state.ptt", true)) return NULL;
      return vfo_state_get_bool(vfo, "cat.state.ptt", false) ? "TX" : "RX";
   }
   const char *key = NULL;
   long divisor = 1;
   int decimals = 0;
   if (!strcmp(field, "freq") || !strcmp(field, "freq_hz")) key = "cat.state.freq";
   else if (!strcmp(field, "freq_khz")) { key = "cat.state.freq"; divisor = 1000; decimals = 3; }
   else if (!strcmp(field, "freq_mhz")) { key = "cat.state.freq"; divisor = 1000000; decimals = 6; }
   else if (!strcmp(field, "width")) key = "cat.state.width";
   else if (!strcmp(field, "power")) key = "cat.state.power";
   if (!key) return NULL;
   long number = vfo_state_get_long(vfo, key, -1);
   if (number < 0) return NULL;
   if (divisor == 1) snprintf(value, size, "%ld", number);
   else snprintf(value, size, "%ld.%0*ld", number / divisor, decimals, number % divisor);
   return value;
}

char *rrclient_tui_topline(tui_window_t *win) {
   // Fetch the raw template: cfg_get_exp() would expand these against config
   // keys rather than current radio state. No cached values survive a redraw.
   const char *format = cfg_get("tui.status-line");
   if (!format) format = RRCLIENT_DEFAULT_STATUS_LINE;
   dict *values = dict_new();
   if (!values) return NULL;

   // Resolve only fields mentioned by the template; no per-VFO state copies.
   const char *p = format;
   while ((p = strstr(p, "${"))) {
      const char *end = strchr(p + 2, '}');
      if (!end) break;
      const char *colon = memchr(p + 2, ':', (size_t)(end - p - 2));
      size_t len = (size_t)((colon ? colon : end) - p - 2);
      char name[128], value[128];
      if (len && len < sizeof(name)) {
         memcpy(name, p + 2, len);
         name[len] = '\0';
         const char *resolved = topline_value(name, win, value, sizeof(value));
         if (resolved) dict_add(values, name, resolved);
      }
      p = end + 1;
   }
   // Reuse the TUI's ${name:fallback} and {color} renderer. The template is
   // data, never a printf format; literal percent signs are safe.
   char *rendered = tui_render_string(values, NULL, "%s", format);
   dict_free(values);
   return rendered;
}
