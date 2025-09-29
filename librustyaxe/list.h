#if	!defined(__librustyaxe_list_h)
#define __librustyaxe_list_h

#include <stdbool.h>

enum rrlist_direction {
   LIST_TAIL = 0,
   LIST_HEAD
};

typedef struct rrlist {
   void          *ptr;
   struct rrlist *prev,
                 *next;
} rrlist_t;

extern rrlist_t *rrlist_find_by_ptr(rrlist_t *list, void *ptr);
extern rrlist_t *rrlist_add(rrlist_t **list, void *ptr, enum rrlist_direction direction);
extern rrlist_t *rrlist_remove(rrlist_t **list, rrlist_t *lp);
extern bool rrlist_destroy(rrlist_t **list);

#endif	// !defined(__librustyaxe_list_h)
