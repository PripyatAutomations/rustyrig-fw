//
// rrclient/command.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#ifndef	__rrclient_cmd_h
#define	__rrclient_cmd_h
#include <librustyaxe/core.h>
#include <librustyaxe/config.h>
#include <librustyaxe/struct.h>
#include <librustyaxe/event-bus.h>

typedef struct client_cmd {
   const char *cmd;
   const char *desc;
   int min_args;
   int max_args;
   bool admin;   // admin-only: hidden from /help and rejected for non-staff
   bool (*cb)(int argc, char **args);
   event_cb_t (*event_cb)(const char *event, void *data, rrconn_t *cptr, void *user);
} client_cmd_t;

// Media channel table iteration for completion providers (media.c owns the
// storage; these walk the stored rrclient_media_known entries).
#define RR_CLIENT_MEDIA_MAX_CHANS 64

struct rr_client_media_chan {
   char uuid[64];
   uint8_t subsystem;
   uint8_t direction;
   uint8_t vfo;
   uint8_t rig;
   char descr[128];
   bool subscribed;
};

// Iterate stored channels: idx 0..n. Returns NULL past the end. `listno`
// (when non-NULL) receives the 1-based list number shown by /media LIST.
extern const struct rr_client_media_chan *rrclient_media_chan_iter(int idx, int *listno);
extern int rrclient_media_chan_count(void);
extern bool media_have_priv(const char *priv);

#ifdef USE_GTK
#include <gtk/gtk.h>
extern bool parse_chat_input_gtk(GtkButton *button, gpointer entry);
#endif

extern bool parse_chat_input_real(const char *msg);
extern bool tui_register_completion_provider(char **(*fn)(const char *line, const char *word));
extern char **client_cmd_completions(const char *line, const char *word);
extern bool cmd_admin(int argc, char **args);
extern bool cmd_chat(int argc, char **args);
extern bool cmd_clear(int argc, char **args);
extern bool cmd_clearlog(int argc, char **args);
extern bool cmd_config(int argc, char **args);
extern bool cmd_die(int argc, char **args);
extern bool cmd_disconnect(int argc, char **args);
extern bool cmd_css_reload(int argc, char **args);   // cfg.gtkcss.c
extern bool cmd_help(int argc, char **args);
extern bool cmd_join(int argc, char **args);
extern bool cmd_kick(int argc, char **args);
extern bool cmd_log(int argc, char **args);
extern bool cmd_media(int argc, char **args);
extern bool media_have_priv(const char *priv);   // media.c
extern bool cmd_me(int argc, char **args);
extern bool cmd_msg(int argc, char **args);
extern bool cmd_names(int argc, char **args);
extern bool cmd_mute(int argc, char **args);
extern bool cmd_notice(int argc, char **args);
extern bool cmd_part(int argc, char **args);
extern bool cmd_quit(int argc, char **args);
extern bool cmd_raw(int argc, char **args);
extern bool cmd_syslog(int argc, char **args);
extern bool cmd_quota(int argc, char **args);
extern bool cmd_rehash(int argc, char **args);
extern bool cmd_restart(int argc, char **args);
extern bool cmd_rxvol(int argc, char **args);
extern bool cmd_server(int argc, char **args);
extern bool cmd_topic(int argc, char **args);
extern bool cmd_unmute(int argc, char **args);
extern bool cmd_whois(int argc, char **args);
extern bool cmd_win(int argc, char **args);

#endif // __rrclient_cmd_h
