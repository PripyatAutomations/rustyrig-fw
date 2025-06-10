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

struct rr_user {
   char   	  name[HTTP_USER_LEN+1];
   char           privs[200];
   time_t	  logged_in;
   time_t         last_heard;
   u_int32_t      user_flags;
   bool           is_ptt;
   bool           is_muted;
   struct rr_user *next;
};

extern bool userlist_add(struct rr_user *cptr);
extern void userlist_clear(void);
extern bool userlist_update(struct rr_user *cptr);
extern bool userlist_remove(const char *name);
extern struct rr_user *global_userlist;
extern struct rr_user *find_or_create_client(const char *name);
extern bool delete_client(struct rr_user *cptr);

#include "inc/client-flags.h"

#endif	// !defined(__rrclient_userlist_h)
