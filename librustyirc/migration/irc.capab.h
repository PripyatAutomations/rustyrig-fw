//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
extern const irc_cap_t irc_capabilities[];
extern void irc_capabilities_reset(rrconn_t *);
extern void irc_capabilities_clear(rrconn_t *);
extern void irc_capabilities_shutdown(void);
extern void irc_capabilities_message(rrconn_t *, const irc_message_t *);
extern bool irc_supports_rustyrig(const rrconn_t *);
extern char irc_prefix_mode(const rrconn_t *, char symbol);
extern const char *irc_modes_symbol(const rrconn_t *, const char *modes);
extern bool irc_mode_has_argument(const rrconn_t *, char mode, bool adding);

/* Borrowed ISUPPORT value until update/reset/disconnect; NULL means absent. */
extern const char *irc_server_property(const rrconn_t *, const char *name);
/* Includes CRLF, bounded by the transport's IRC_MSGLEN buffer. */
extern size_t irc_line_limit(const rrconn_t *);

/* CAP names are case-sensitive. Values are borrowed like ISUPPORT values. */
extern const char *irc_server_capability(const rrconn_t *, const char *name);
extern bool irc_capability_enabled(const rrconn_t *, const char *name);
extern const char *irc_server_capab(const rrconn_t *, const char *name);

extern void irc_capabilities_poll(rrconn_t *);
