/* Ephemeral user presence for native RustyRig rooms. No persistence,
 * history playback, GTK dependency, or global nickname-only lookups. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <librustyaxe/core.h>
#include <rrclient/typing.h>
#include <rrclient/frontend.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>

#define MAX_TYPING_LEASES 512
#define MAX_TYPING_TTL_MS 15000

typedef struct typing_lease {
   struct typing_lease *next;
   const rrconn_t *connection;
   char room[128], name[HTTP_USER_LEN + 1], epoch[64], previous_epoch[64];
   uint64_t sequence, expires_ms;
   rr_typing_state_t state;
} typing_lease_t;

static typing_lease_t *typing_leases;
static unsigned typing_lease_count;

static void typing_notify(void) {
   if (frontend_ops() && rrclient_present_context())
      frontend_ops()->userlist_redraw();
}

bool rrclient_typing_prune(void) {
   const uint64_t now_ms = (uint64_t)mono_ms();
   bool changed = false;
   typing_lease_t **p = &typing_leases;
   while (*p) {
      typing_lease_t *lease = *p;
      if (now_ms >= lease->expires_ms) {
         *p = lease->next;
         changed |= lease->state != RR_TYPING_IDLE;
         free(lease);
         typing_lease_count--;
      } else {
         p = &lease->next;
      }
   }
   return changed;
}

rr_typing_state_t rrclient_typing_get(const rrconn_t *connection, const char *room, const char *name) {
   if (!connection || !room || !name) return RR_TYPING_IDLE;
   uint64_t at = (uint64_t)mono_ms();
   for (typing_lease_t *l = typing_leases; l; l = l->next)
      if (l->connection == connection && !strcasecmp(l->room, room) &&
          !strcasecmp(l->name, name) && at < l->expires_ms)
         return l->state;
   return RR_TYPING_IDLE;
}

void rrclient_typing_event(const char *event, const char *json, rrconn_t *connection, void *user) {
   (void)user;
   if (!connection) return;
   if (!strcmp(event, "disconnected")) {
      bool changed = false;
      typing_lease_t **p = &typing_leases;
      while (*p) {
         typing_lease_t *l = *p;
         if (l->connection == connection) {
            *p = l->next;
            free(l);
            typing_lease_count--;
            changed = true;
         } else p = &l->next;
      }
      if (changed) typing_notify();
      return;
   }
   if (!connection->is_ws || !json) return;
   dict *d = json2dict(json);
   if (!d) return;
   const char *room = dict_get(d, "talk.room", "");
   const char *name = dict_get(d, "talk.from", "");
   const char *state = dict_get(d, "talk.typing", "");
   const char *epoch = dict_get(d, "talk.epoch", "");
   unsigned long long seq = dict_get_ullong(d, "talk.seq", 0);
   unsigned ttl = (unsigned)dict_get_ulong(d, "talk.ttl", 0);
   rr_typing_state_t kind = !strcmp(state, "started") ? RR_TYPING_STARTED :
                             !strcmp(state, "active") ? RR_TYPING_ACTIVE :
                             !strcmp(state, "paused") ? RR_TYPING_PAUSED : RR_TYPING_IDLE;
   if (!*room || !*name || !*epoch || strlen(room) >= 128 ||
       strlen(name) > HTTP_USER_LEN || strlen(epoch) >= 64 || !seq ||
       (strcmp(state, "started") && strcmp(state, "active") &&
        strcmp(state, "paused") && strcmp(state, "idle"))) {
      dict_free(d);
      return;
   }
   bool changed = rrclient_typing_prune();
   typing_lease_t **p = &typing_leases;
   while (*p && !((*p)->connection == connection && !strcasecmp((*p)->room, room) &&
                  !strcasecmp((*p)->name, name))) p = &(*p)->next;
   typing_lease_t *lease = *p;
   if (lease && !strcmp(lease->epoch, epoch) && seq <= lease->sequence) {
      dict_free(d);
      if (changed) typing_notify();
      return;
   }
   /* Keep an idle tombstone until the reordering window expires. Without
    * it an older in-flight 'active' update could resurrect stale presence. */
   if (!lease && typing_lease_count < MAX_TYPING_LEASES) {
      lease = calloc(1, sizeof(*lease));
      if (lease) {
         lease->next = typing_leases;
         typing_leases = lease;
         typing_lease_count++;
      }
   }
   if (lease) {
      /* A previously replaced incarnation must never take over again. */
      if (*lease->previous_epoch && !strcmp(lease->previous_epoch, epoch)) {
         dict_free(d);
         if (changed) typing_notify();
         return;
      }
      if (*lease->epoch && strcmp(lease->epoch, epoch))
         snprintf(lease->previous_epoch, sizeof(lease->previous_epoch), "%s", lease->epoch);
      changed |= lease->state != kind;
      lease->connection = connection;
      snprintf(lease->room, sizeof(lease->room), "%s", room);
      snprintf(lease->name, sizeof(lease->name), "%s", name);
      snprintf(lease->epoch, sizeof(lease->epoch), "%s", epoch);
      lease->sequence = seq;
      lease->state = kind;
      const unsigned lease_ms = kind == RR_TYPING_IDLE || !ttl ? MAX_TYPING_TTL_MS :
         (ttl > MAX_TYPING_TTL_MS / 1000u ? MAX_TYPING_TTL_MS : ttl * 1000u);
      lease->expires_ms = (uint64_t)mono_ms() + lease_ms;
   }
   dict_free(d);
   if (changed) typing_notify();
}
