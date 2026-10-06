#ifndef RRCLIENT_RESOURCE_CONTEXT_H
#define	RRCLIENT_RESOURCE_CONTEXT_H
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* PARITY: rustyrig-www/js/webui.media.js mediaResourceMatches. NULL/status sees all. Rig
 * RX rooms share the base rig's resources. */
static inline bool rrclient_resource_matches(const char *context, const char *room) {
   if (!context || !*context || context[0] != '#') { return true; }

   if (!room || !*room) { return false; }
   size_t length = strlen(context);
   const char *rig = NULL;

   for (const char *p = context ; ( p = strchr(p, '-') ) ; p++) {
      if ( !strncasecmp(p, "-rig", 4) && isdigit( (unsigned char)p[4] ) ) { rig = p; }
   }

   if (rig) {
      const char *end = rig + 4;
      while ( isdigit( (unsigned char)*end ) ) { end++; }

      if (!*end || *end == '.') { length = (size_t)(end - context); } else { rig = NULL; }
   }

   if ( strncasecmp(context, room, length) ) { return false; }

   if (!room[length]) { return true; }

   if (rig) { return room[length] == '.' && room[length + 1]; }
   const char *suffix = room + length;

   if ( strncasecmp(suffix, "-rig", 4) || !isdigit( (unsigned char)suffix[4] ) ) { return false; }
   suffix += 4;
   while ( isdigit( (unsigned char)*suffix ) ) { suffix++; }
   return !*suffix || (*suffix == '.' && suffix[1]);
}
#endif
