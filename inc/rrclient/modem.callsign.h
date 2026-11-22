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
/* Shared mode and RF band naming for watched-call highlighting and events. */
static inline const char *rr_modem_callsign_band(uint64_t hz) {
   static const struct {uint64_t lo,hi;const char *name;} ranges[]={
      {135700,137800,"2200m"},{472000,479000,"630m"},
      {1800000,2000000,"160m"},{3500000,4000000,"80m"},
      {5330000,5410000,"60m"},{7000000,7300000,"40m"},
      {10100000,10150000,"30m"},{14000000,14350000,"20m"},
      {18068000,18168000,"17m"},{21000000,21450000,"15m"},
      {24890000,24990000,"12m"},{28000000,29700000,"10m"},
      {50000000,54000000,"6m"},{144000000,148000000,"2m"},
      {222000000,225000000,"1.25m"},{420000000,450000000,"70cm"},
      {902000000,928000000,"33cm"},{1240000000,1300000000,"23cm"}
   };
   for(guint i=0;i<G_N_ELEMENTS(ranges);i++)
      if(hz>=ranges[i].lo&&hz<=ranges[i].hi)return ranges[i].name;
   return NULL;
}
static inline const char *rr_modem_callsign_mode(const char *engine,const char *mode) {
   if(!engine)return "";
   if(!g_ascii_strcasecmp(engine,"ft8"))return "FT8";
   if(!g_ascii_strcasecmp(engine,"olivia"))return "Olivia";
   if(!g_ascii_strcasecmp(engine,"psk"))return mode&&*mode?mode:"PSK";
   return mode&&*mode?mode:engine;
}
#endif
