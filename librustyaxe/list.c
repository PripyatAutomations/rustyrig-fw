#include <librustyaxe/core.h>
rrlist_t *rrlist_find_by_ptr(rrlist_t *list, void *ptr) {
   if (!list || !ptr) {
      return NULL;
   }
   for (rrlist_t *p = list; p; p = p->next) {
      if (p->ptr == ptr) {
         return p;
      }
   }
   return NULL;
}

rrlist_t *rrlist_add(rrlist_t **list, void *ptr, enum rrlist_direction direction) {
   if (!list || !ptr) {
      return NULL;
   }

   rrlist_t *np = calloc(1, sizeof(rrlist_t));
   if (!np) {
      fprintf(stderr, "OOM in rrlist_add\n");
      return NULL;
   }
   np->ptr = ptr;

   if (*list == NULL) {
      *list = np;
      return np;
   }

   if (direction == LIST_HEAD) {
      np->next = *list;
      (*list)->prev = np;
      *list = np;
   } else if (direction == LIST_TAIL) {
      rrlist_t *lp = *list;
      while (lp->next) {
         lp = lp->next;
      }
      lp->next = np;
      np->prev = lp;
   } else {
      fprintf(stderr, "rrlist_add invalid direction: %d\n", direction);
      free(np);
      return NULL;
   }
   return np;
}

rrlist_t *rrlist_remove(rrlist_t **list, rrlist_t *lp) {
   if (!list || !*list || !lp) {
      return NULL;
   }

   if (lp->prev) {
      lp->prev->next = lp->next;
   }

   if (lp->next) {
      lp->next->prev = lp->prev;
   }

   if (lp == *list) {
      *list = lp->next;
   }

   rrlist_t *next = lp->next;
   free(lp);
   return next;
}

bool rrlist_destroy(rrlist_t **list) {
   if (!list || !*list) {
      return true;
   }

   rrlist_t *p = *list;
   while (p) {
      rrlist_t *next = p->next;
      free(p);
      p = next;
   }
   *list = NULL;
   return true;
}
