#if	!defined(__rr_client_flags_h)
#define	__rr_client_flags_h
#include <stdbool.h>
//
// This needs cleaned up to not depend on rrclient/userlist.h

#define FLAG_ADMIN       0x00000001		// NYI: Is an admin
#define	FLAG_OWNER	 0x00000002
#define FLAG_MUTED       0x00000004		// NYI: FLag set when muted
#define FLAG_PTT         0x00000008		// NYI: Flag set when PTT active
#define FLAG_SERVERBOT   0x00000010
#define FLAG_STAFF       0x00000020
#define FLAG_SUBSCRIBER  0x00000040
#define FLAG_LISTENER    0x00000080
#define	FLAG_SYSLOG	 0x00000100
#define	FLAG_CAN_TX	 0x00000200
#define	FLAG_NOOB        0x00000400		// user can only use ws.cat if owner|admin logged in
#define	FLAG_ELMER       0x00000800		// user is an elmer, so noobs can TX if they are present

#if	!defined(__RRCLIENT)
#include "common/http.h"
extern bool client_has_flag(http_client_t *cptr, uint32_t user_flag);
extern void client_set_flag(http_client_t *cptr, uint32_t flag);
extern void client_clear_flag(http_client_t *cptr, uint32_t flag);
#else
static inline bool has_flag(struct rr_user *cptr, uint32_t user_flag) {
   if (cptr) {
      return (cptr->user_flags & user_flag) != 0;
   }

   return false;
}

static inline void set_flag(struct rr_user *cptr, uint32_t flag) {
   if (cptr) {
      cptr->user_flags |= flag;
   }
}

static inline void clear_flag(struct rr_user *cptr, uint32_t flag) {
   if (cptr) {
      cptr->user_flags &= ~flag;
   }
}
#endif	// __RRCLIENT

#endif	// !defined(__rr_client_flags_h)
