// Native IRC session observations; no network requests or GTK dependencies.
#ifndef RRCLIENT_IRC_STATS_H
#define RRCLIENT_IRC_STATS_H
#include <stdint.h>
#include <time.h>
#include <librrprotocol/rrprotocol.h>
struct rrclient_irc_channel_stats {
   const char *name, *topic;
   bool joined, listed;
   unsigned listed_users;
   time_t joined_at;
   uint64_t joined_us, seen, sent, highlights;
};
/* Borrowed client-lifetime snapshot, iterate with previous=NULL for each server profile. */
const struct rrclient_irc_channel_stats *rrclient_irc_stats_next(const char *profile, const struct rrclient_irc_channel_stats *previous);
void rrclient_irc_stats_register(void);
void rrclient_irc_stats_highlight(rrconn_t *connection, const char *room);
#endif
