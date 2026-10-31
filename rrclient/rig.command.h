#ifndef RRCLIENT_RIG_COMMAND_H
#define RRCLIENT_RIG_COMMAND_H
/* Transport/UI neutral parser for ordered stacked ! rig commands. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
typedef enum { RR_RIG_VFO, RR_RIG_FREQ, RR_RIG_MODE, RR_RIG_WIDTH, RR_RIG_STATUS } rr_rig_cmd_kind_t;
typedef struct { rr_rig_cmd_kind_t kind; char value[64]; } rr_rig_cmd_t;
typedef bool (*rr_rig_cmd_fn)(const rr_rig_cmd_t *, void *);
static inline int rr_rig_cmd_kind(const char *s) {
   if (!s) return -1;
   if (*s == '!') s++;
   if (!strcasecmp(s,"vfo")) return RR_RIG_VFO;
   if (!strcasecmp(s,"freq")) return RR_RIG_FREQ;
   if (!strcasecmp(s,"mode")) return RR_RIG_MODE;
   if (!strcasecmp(s,"width")) return RR_RIG_WIDTH;
   if (!strcasecmp(s,"status")) return RR_RIG_STATUS;
   return -1;
}
static inline bool rr_rig_parse_frequency(const char *s, uint64_t *hz) {
   if (!s || !*s || !hz || *s == '-') return false;
   char *end; errno = 0;
   double value = strtod(s, &end);
   if (end == s || errno == ERANGE || !isfinite(value) || value <= 0) return false;
   double scale;
   if (!*end) scale = value < 100000 ? 1000. : 1.;
   else if (!strcasecmp(end,"m") || !strcasecmp(end,"mhz")) scale = 1000000.;
   else if (!strcasecmp(end,"k") || !strcasecmp(end,"khz")) scale = 1000.;
   else if (!strcasecmp(end,"hz")) scale = 1.;
   else return false;
   double f = value * scale;
   if (!isfinite(f) || f < 1. || f >= 18446744073709551616.) return false;
   *hz = (uint64_t)(f + .5);
   return *hz != 0;
}
/* Callback is invoked in order, e.g. !vfo B freq 7200 mode USB.
 * False from callback stops the chain. No GUI, network or rig dependencies. */
static inline bool rr_rig_parse_commands(const char *line, rr_rig_cmd_fn fn, void *ctx) {
   if (!line || *line != '!' || !fn) return false;
   const char *p = line;
   bool any = false;
   while (*p) {
      while (isspace((unsigned char)*p)) ++p;
      if (!*p) break;
      char word[32] = "";
      size_t n = 0;
      while (*p && !isspace((unsigned char)*p)) {
         if (n + 1 >= sizeof(word)) return false;
         word[n++] = *p++;
      }
      int kind = rr_rig_cmd_kind(word);
      if (kind < 0) return false;
      rr_rig_cmd_t cmd = {.kind = kind, .value = ""};
      while (isspace((unsigned char)*p)) ++p;
      if (kind != RR_RIG_STATUS) {
         if (!*p) return false;
         n = 0;
         while (*p && !isspace((unsigned char)*p)) {
            if (n + 1 >= sizeof(cmd.value)) return false;
            cmd.value[n++] = *p++;
         }
         if (rr_rig_cmd_kind(cmd.value) >= 0) return false;
      }
      if (!fn(&cmd, ctx)) return false;
      any = true;
   }
   return any;
}
#endif
