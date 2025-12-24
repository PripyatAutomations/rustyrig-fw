#if	!defined(__irc_channel_h)
#define __irc_channel_h

extern unsigned irc_hash_nick(const char *nick);
extern irc_chan_user_t *chan_find_user(irc_channel_t *chan, const char *nick);
extern irc_chan_user_t *chan_add_user(irc_channel_t *chan, const char *raw);
extern void chan_remove_user(irc_channel_t *chan, const char *nick);
extern void chan_clear_users(irc_channel_t *chan);
extern void chan_begin_names(irc_channel_t *chan);
extern void chan_end_names(irc_channel_t *chan);
extern void irc_handle_353(irc_channel_t *chan, const char *names);
extern void handle_numeric_353(irc_channel_t *chan, const irc_message_t *msg);
extern void handle_join(irc_channel_t *chan, const irc_message_t *msg);
extern void handle_part_or_quit(irc_channel_t *chan, const irc_message_t *msg);
extern void handle_nick_change(irc_channel_t *chan, const irc_message_t *msg);

#endif	// !defined(__irc_channel_h)
