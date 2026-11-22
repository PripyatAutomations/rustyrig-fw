/* Complete callsign token matching shared by modem core and GTK presentation.
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef RRCLIENT_MODEM_CALLSIGN_H
#define RRCLIENT_MODEM_CALLSIGN_H
#include <glib.h>
#include <string.h>
static inline gboolean rr_modem_callsign_in_text(const char *text,const char *call) {
   if(!text||!call||!*call)return FALSE;
   size_t n=strlen(call);
   for(const char *p=text;*p;p++) {
      if(g_ascii_strncasecmp(p,call,n))continue;
      if(p!=text&&(g_ascii_isalnum(p[-1])||p[-1]=='/'))continue;
      if(g_ascii_isalnum(p[n])||p[n]=='/')continue;
      return TRUE;
   }
   return FALSE;
}
#endif
