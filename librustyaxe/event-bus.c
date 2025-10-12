//
// event-bus.c: Here we implement a way to hook various events by name
#include <librustyaxe/core.h>
#include <librustyaxe/kvstore.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/kvstore.h>

static kv_store_t *event_store = NULL;

void event_init(void) {
   if (!event_store) {
      event_store = kv_create(65536, KV_BST);
   }
}

/* subscribe */
void event_on(const char *event, event_cb_t cb, void *user) {
   if (!event_store || !event) {
      return;
   }

   kv_list_t *list = kv_lookup(event_store, event);
   if (!list) {
      list = calloc(1, sizeof(*list));
      list->type = KV_ARRAY;
      kv_insert(event_store, event, list);
   }

   event_listener_t *l = calloc(1, sizeof(*l));
   l->cb = cb;
   l->user = user;

   list->ptr = realloc(list->ptr, sizeof(void*) * (list->count + 1));
   ((void**)list->ptr)[list->count++] = l;
}

/* emit */
void event_emit(const char *event, irc_client_t *cptr, void *data) {
   if (!event_store || !event) {
      return;
   }

   kv_list_t *list = kv_lookup(event_store, event);
   if (!list) {
      return;
   }

   for (size_t i = 0; i < list->count; i++) {
      event_listener_t *l = ((void**)list->ptr)[i];
      l->cb(event, data, cptr, l->user);
   }
}

/* unsubscribe */
void event_off(const char *event, event_cb_t cb, void *user) {
   if (!event_store || !event) {
      return;
   }

   kv_list_t *list = kv_lookup(event_store, event);
   if (!list) {
      return;
   }

   for (size_t i = 0; i < list->count; ) {
      event_listener_t *l = ((void**)list->ptr)[i];
      if ((!cb || l->cb == cb) && (!user || l->user == user)) {
         free(l);
         memmove(&((void**)list->ptr)[i], &((void**)list->ptr)[i + 1],
                 (list->count - i - 1) * sizeof(void*));
         list->count--;
         continue;
      }
      i++;
   }

   if (list->count == 0) {
      kv_remove(event_store, event);
      free(list->ptr);
      free(list);
   }
}

/* optional cleanup */
void event_shutdown(void) {
   if (!event_store) {
      return;
   }

   kv_destroy(event_store);
   event_store = NULL;
}
