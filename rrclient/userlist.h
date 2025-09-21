//
// inc/rrclient/userlist.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rrclient_userlist_h)
#define	__rrclient_userlist_h

#if	defined(USE_GTK)
#include <gtk/gtk.h>
#endif

#include <librrprotocol/rrprotocol.h>

enum {
   COL_PRIV_ICON,
   COL_USERNAME,
   COL_TALK_ICON,
   COL_MUTE_ICON,
   COL_ELMERNOOB_ICON,
   NUM_COLS
};

static inline const char *select_user_icon(struct rr_user *cptr) {
   if (strcasestr(cptr->privs, "owner")) { return "👑"; }
   if (strcasestr(cptr->privs, "admin")) { return "⭐"; }
   if (strcasestr(cptr->privs, "tx")) { return "👤"; }
   return "👀";
}

static inline const char *select_elmernoob_icon(struct rr_user *cptr) {
   if (strcasestr(cptr->privs, "elmer")) { return "🧙"; }
   if (strcasestr(cptr->privs, "noob")) { return "🐣"; }
   return "";
}

extern struct rr_user *global_userlist;
extern bool userlist_add_or_update(const struct rr_user *newinfo);
extern bool userlist_remove_by_name(const char *name);
extern void userlist_clear_all(void); // destroys the list
extern struct rr_user *userlist_find(const char *name);
extern void userlist_redraw_gtk(void);


extern struct rr_user *find_client(const char *name);
extern struct rr_user *find_or_create_client(const char *name);
extern bool delete_client(struct rr_user *cptr);
extern bool clear_client_list(void);
extern struct rr_user *userlist_find(const char *name);

#if	defined(USE_GTK)
extern GtkWidget *userlist_init(void);
#endif	// defined(USE_GTK)

#include <librrprotocol/client-flags.h>

#endif	// !defined(__rrclient_userlist_h)
