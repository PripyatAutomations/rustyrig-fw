// rrserver/rig.registry.c: runtime collection of independently-owned rigs
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdlib.h>
#include <string.h>

#include <rrserver/rig.registry.h>

typedef struct rr_rig_registry_entry {
   char *alias;
   rr_server_rig_t *radio;
   struct rr_rig_registry_entry *next;
} rr_rig_registry_entry_t;

struct rr_rig_registry {
   rr_rig_registry_entry_t *head;
   rr_server_rig_t *legacy;
   size_t count;
};

rr_rig_registry_t *rr_rig_registry_new(void) {
   return calloc(1, sizeof(rr_rig_registry_t));
}

static void rr_rig_registry_entry_free(rr_rig_registry_entry_t *entry) {
   if (!entry) {
      return;
   }
   rr_backend_instance_free(rr_server_rig_backend(entry->radio));
   rr_server_rig_set_backend(entry->radio, NULL);
   rr_server_rig_free(entry->radio);
   free(entry->alias);
   free(entry);
}

void rr_rig_registry_free(rr_rig_registry_t *registry) {
   if (!registry) {
      return;
   }
   rr_rig_registry_entry_t *entry = registry->head;
   while (entry) {
      rr_rig_registry_entry_t *next = entry->next;
      rr_rig_registry_entry_free(entry);
      entry = next;
   }
   free(registry);
}

rr_server_rig_t *rr_rig_registry_find_uuid(const rr_rig_registry_t *registry,
   const char *uuid) {
   if (!registry || !uuid || !*uuid) {
      return NULL;
   }
   for (rr_rig_registry_entry_t *entry = registry->head; entry;
        entry = entry->next) {
      if (strcmp(rr_server_rig_id(entry->radio), uuid) == 0) {
         return entry->radio;
      }
   }
   return NULL;
}

rr_server_rig_t *rr_rig_registry_find_alias(const rr_rig_registry_t *registry,
   const char *alias) {
   if (!registry || !alias || !*alias) {
      return NULL;
   }
   for (rr_rig_registry_entry_t *entry = registry->head; entry;
        entry = entry->next) {
      if (strcmp(entry->alias, alias) == 0) {
         return entry->radio;
      }
   }
   return NULL;
}

rr_server_rig_t *rr_rig_registry_add(rr_rig_registry_t *registry,
   const char *uuid, const char *alias, const char *name,
   const rr_backend_type_t *backend_type) {
   if (!registry || !uuid || !*uuid || !alias || !*alias || !backend_type ||
       rr_rig_registry_find_uuid(registry, uuid) ||
       rr_rig_registry_find_alias(registry, alias)) {
      return NULL;
   }

   rr_rig_registry_entry_t *entry = calloc(1, sizeof(*entry));
   rr_server_rig_t *radio = rr_server_rig_new(uuid, name);
   if (!entry || !radio) {
      free(entry);
      rr_server_rig_free(radio);
      return NULL;
   }
   entry->alias = strdup(alias);
   if (!entry->alias) {
      rr_server_rig_free(radio);
      free(entry);
      return NULL;
   }
   rr_backend_t *backend = rr_backend_instance_new(backend_type, radio, alias);
   if (!backend) {
      rr_server_rig_free(radio);
      free(entry->alias);
      free(entry);
      return NULL;
   }
   rr_server_rig_set_backend(radio, backend);
   entry->radio = radio;
   entry->next = registry->head;
   registry->head = entry;
   registry->count++;
   return radio;
}

bool rr_rig_registry_remove(rr_rig_registry_t *registry, const char *uuid) {
   if (!registry || !uuid || !*uuid) {
      return true;
   }
   rr_rig_registry_entry_t **link = &registry->head;
   while (*link) {
      rr_rig_registry_entry_t *entry = *link;
      if (strcmp(rr_server_rig_id(entry->radio), uuid) != 0) {
         link = &entry->next;
         continue;
      }
      /* Compatibility adapters borrow the explicit legacy rig. Its owner
         must clear that designation (and free adapters) before removal. */
      if (registry->legacy == entry->radio) {
         return true;
      }
      *link = entry->next;
      registry->count--;
      rr_rig_registry_entry_free(entry);
      return false;
   }
   return true;
}

const char *rr_rig_registry_alias(const rr_rig_registry_t *registry,
   const rr_server_rig_t *radio) {
   if (!registry || !radio) {
      return NULL;
   }
   for (rr_rig_registry_entry_t *entry = registry->head; entry;
        entry = entry->next) {
      if (entry->radio == radio) {
         return entry->alias;
      }
   }
   return NULL;
}

size_t rr_rig_registry_count(const rr_rig_registry_t *registry) {
   return registry ? registry->count : 0;
}

bool rr_rig_registry_foreach(rr_rig_registry_t *registry,
   rr_rig_registry_iter_fn callback, void *user) {
   if (!registry || !callback) {
      return true;
   }
   bool failed = false;
   for (rr_rig_registry_entry_t *entry = registry->head; entry;
        entry = entry->next) {
      if (callback(entry->radio, user)) {
         failed = true;
      }
   }
   return failed;
}

bool rr_rig_registry_set_legacy(rr_rig_registry_t *registry,
   rr_server_rig_t *radio) {
   if (!registry || (radio && !rr_rig_registry_alias(registry, radio))) {
      return true;
   }
   registry->legacy = radio;
   return false;
}

rr_server_rig_t *rr_rig_registry_legacy(const rr_rig_registry_t *registry) {
   return registry ? registry->legacy : NULL;
}
