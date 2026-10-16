// Native IRC channel observations. Part of rustyrig-fw; MIT without mongoose, GPL with it.
#include <stdlib.h>
#include <stddef.h>
#include <glib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <rrclient/connman.h>
#include <rrclient/irc.stats.h>
extern time_t now;
struct channel_stats {
   struct channel_stats *next;
   char *profile;
   struct rrclient_irc_channel_stats value;
};
static struct channel_stats *channels;
static GHashTable *channel_index;

static struct rrclient_irc_channel_stats *channel_get(rrconn_t *connection, const char *name) {
   const char *profile = rrclient_connection_name(connection);
   if (!profile || !name || (name[0] != '#' && name[0] != '&')) return NULL;
   if (!channel_index) channel_index = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   char *folded = g_ascii_strdown(name, -1);
   char *key = g_strdup_printf("%s\n%s", profile, folded);
   g_free(folded);
   struct channel_stats *known = g_hash_table_lookup(channel_index, key);
   if (known) { g_free(key); return &known->value; }
   struct channel_stats *row = calloc(1, sizeof(*row));
   if (!row) { g_free(key); return NULL; }
   row->profile = strdup(profile);
   row->value.name = strdup(name);
   row->value.topic = strdup("");
   if (!row->profile || !row->value.name || !row->value.topic) {
      free(row->profile); free((char *)row->value.name); free((char *)row->value.topic); free(row);
      g_free(key);
      return NULL;
   }
   g_hash_table_insert(channel_index, key, row);
   row->next = channels;
   channels = row;
   return &row->value;
}
const struct rrclient_irc_channel_stats *rrclient_irc_stats_next(const char *profile, const struct rrclient_irc_channel_stats *previous) {
   const struct channel_stats *start = previous ?
      ((const struct channel_stats *)((const char *)previous - offsetof(struct channel_stats, value)))->next : channels;
   for (const struct channel_stats *row = start; row; row = row->next) {
      if (!strcmp(row->profile, profile)) return &row->value;
   }
   return NULL;
}
void rrclient_irc_stats_highlight(rrconn_t *connection, const char *room) {
   if (!connection || connection->is_ws) return;
   struct rrclient_irc_channel_stats *stats = channel_get(connection, room);
   if (stats) stats->highlights++;
}
static void stats_event(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)unused;
   if (!connection || connection->is_ws) return;
   if (!strcmp(event, "irc.disconnected") || !strcmp(event, "irc.connected")) {
      const char *profile = rrclient_connection_name(connection);
      for (struct channel_stats *row = channels; profile && row; row = row->next) {
         if (strcmp(row->profile, profile)) continue;
         row->value.joined = row->value.listed = false;
         row->value.joined_at = 0; row->value.joined_us = 0;
      }
      return;
   }
   dict *message = data ? json2dict(data) : NULL;
   if (!message) return;
   if (!strcmp(event, "irc.sent")) {
      struct rrclient_irc_channel_stats *stats = channel_get(connection, dict_get(message, "talk.target", ""));
      if (stats) stats->sent++;
      dict_free(message);
      return;
   }
   const char *cmd = dict_get(message, "msg.cmd", "");
   const char *arg1 = dict_get(message, "msg.arg1", "");
   const char *arg2 = dict_get(message, "msg.arg2", "");
   const char *prefix = dict_get(message, "msg.prefix", "");
   size_t nicklen = strcspn(prefix, "!");
   bool self = strlen(connection->nick) == nicklen && !strncasecmp(prefix, connection->nick, nicklen);
   if (!strcmp(cmd, "321")) {
      const char *profile = rrclient_connection_name(connection);
      for (struct channel_stats *row = channels; profile && row; row = row->next) {
         if (!strcmp(row->profile, profile)) row->value.listed = false;
      }
   }
   struct rrclient_irc_channel_stats *stats = channel_get(connection, !strcmp(cmd, "322") ? arg2 : arg1);
   if (stats) {
      if (!strcmp(cmd, "322")) {
         stats->listed = true;
         stats->listed_users = dict_get_uint(message, "msg.arg3", 0);
         char *topic = strdup(dict_get(message, "msg.arg4", ""));
         if (topic) { free((char *)stats->topic); stats->topic = topic; }
      } else if (!strcasecmp(cmd, "JOIN") && self) {
         stats->joined = true; stats->joined_at = now; stats->joined_us = mono_us();
      } else if ((!strcasecmp(cmd, "PART") && self) || (!strcasecmp(cmd, "KICK") && !strcasecmp(arg2, connection->nick))) {
         stats->joined = false; stats->joined_at = 0; stats->joined_us = 0;
      } else if ((!strcasecmp(cmd, "PRIVMSG") || !strcasecmp(cmd, "NOTICE")) && !self) {
         stats->seen++;
      }
   }
   dict_free(message);
}
void rrclient_irc_stats_register(void) {
   event_on("irc.message", stats_event, NULL);
   event_on("irc.sent", stats_event, NULL);
   event_on("irc.connected", stats_event, NULL);
   event_on("irc.disconnected", stats_event, NULL);
}
