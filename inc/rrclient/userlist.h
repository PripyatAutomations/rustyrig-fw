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
#define	HTTP_USER_LEN		16		// username length (16 char)
#define	HTTP_PASS_LEN		40		// sha1: 40, sha256: 64
#define	HTTP_HASH_LEN		40		// sha1
#define	HTTP_TOKEN_LEN		14		// session-id / nonce length, longer moar secure
#define	HTTP_UA_LEN		512		// allow 128 bytes
#define	USER_PRIV_LEN		100		// privileges list
#define USER_EMAIL_LEN		128		// email address

#if	defined(USE_GTK)
#include <gtk/gtk.h>
#endif

struct rr_user {
   char   	  name[HTTP_USER_LEN+1];
   char           privs[200];
   time_t	  logged_in;
   time_t         last_heard;
   uint32_t       user_flags;
   int            clones;
   bool           is_ptt;
   bool           is_muted;
#if	defined(USE_GTK)
   GtkTreeIter iter;   // <-- GTK list row reference
#endif
   bool in_store;      // <-- whether `iter` is valid

   struct rr_user *next;
};

extern struct rr_user *global_userlist;
extern bool userlist_add_or_update(const struct rr_user *newinfo);
extern bool userlist_remove_by_name(const char *name);
extern void userlist_clear_all(void); // destroys the list
extern struct rr_user *userlist_find(const char *name);
extern void userlist_redraw_gtk(void);
//
extern struct rr_user *find_client(const char *name);
extern struct rr_user *find_or_create_client(const char *name);
extern bool delete_client(struct rr_user *cptr);
extern bool clear_client_list(void);
extern struct rr_user *userlist_find(const char *name);

#include "common/client-flags.h"

#endif	// !defined(__rrclient_userlist_h)
