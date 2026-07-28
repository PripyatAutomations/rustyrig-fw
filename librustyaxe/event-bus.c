//
// event-bus.c: Here we implement a way to hook various events by name
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

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
      // XXX: Make this more graceful
      if (!list) {
         abort();
      }
      list->type = KV_ARRAY;
      kv_insert(event_store, event, list);
   }

   event_listener_t *l = calloc(1, sizeof(*l));
   // XXX: make this more graceful
   if (!l) {
      abort();
   }
   l->cb = cb;
   l->user = user;

   list->ptr = realloc(list->ptr, sizeof(void*) * (list->count + 1));
   // XXX: make this more graceful
   if (!list->ptr) {
      abort();
   }
   ((void**)list->ptr)[list->count++] = l;
}

/* emit */
void event_emit(const char *event, irc_conn_t *cptr, void *data) {
   if (!event_store || !event) {
      return;
   }

   kv_list_t *list = kv_lookup(event_store, event);
   if (!list) {
      return;
   }

   for (size_t i = 0; i < list->count; i++) {
      event_listener_t *l = ((void**)list->ptr)[i];
      Log(LOG_CRAZY, "event", "Event %s from cptr:<%p> with data:<%p> user:<%p", event, cptr, data, l->user);
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
