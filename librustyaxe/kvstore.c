//
// Test of faster storage type 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <librustyaxe/kvstore.h>

// ---------------- helpers ----------------
static uint16_t prefix_index_key(const char *key) {
   return ((uint8_t)key[0] << 8) | (uint8_t)key[1];
}

static int kv_array_bsearch(kv_node_t **arr, size_t count, const char *key) {
   size_t lo = 0;
   size_t hi = count;

   while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      int cmp = strcmp(key, arr[mid]->key);

      if (cmp == 0) {
         return (int)mid;
      }

      if (cmp < 0) {
         hi = mid;
      } else {
         lo = mid + 1;
      }
   }

   return -(int)(lo + 1);
}

static kv_node_t *bst_insert(kv_node_t *node, const char *key, void *val) {
   if (!node) {
      kv_node_t *n = calloc(1, sizeof(*n));
      n->key = strdup(key);
      n->value = val;
      return n;
   }

   int cmp = strcmp(key, node->key);

   if (cmp == 0) {
      node->value = val;
      return node;
   }

   if (cmp < 0) {
      node->left = bst_insert(node->left, key, val);
   } else {
      node->right = bst_insert(node->right, key, val);
   }

   return node;
}

static void *bst_lookup(kv_node_t *node, const char *key) {
   while (node) {
      int cmp = strcmp(key, node->key);

      if (cmp == 0) {
         return node->value;
      }

      if (cmp < 0) {
         node = node->left;
      } else {
         node = node->right;
      }
   }

   return NULL;
}

static void bst_free(kv_node_t *node) {
   if (!node) {
      return;
   }

   bst_free(node->left);
   bst_free(node->right);
   free(node->key);
   free(node);
}

static kv_node_t *bst_remove(kv_node_t *node, const char *key, int *removed) {
   if (!node) return NULL;

   int cmp = strcmp(key, node->key);

   if (cmp < 0) {
      node->left = bst_remove(node->left, key, removed);
   } else if (cmp > 0) {
      node->right = bst_remove(node->right, key, removed);
   } else {
      *removed = 1;

      if (!node->left) {
         kv_node_t *r = node->right;
         free(node->key);
         free(node);
         return r;
      }

      if (!node->right) {
         kv_node_t *l = node->left;
         free(node->key);
         free(node);
         return l;
      }

      kv_node_t *succ = node->right;

      while (succ->left) {
         succ = succ->left;
      }

      free(node->key);
      node->key = strdup(succ->key);
      node->value = succ->value;
      node->right = bst_remove(node->right, succ->key, removed);
   }

   return node;
}

// ----------------- public API -----------------
kv_store_t *kv_create(size_t prefix_size, kv_type_t type) {
   kv_store_t *store = calloc(1, sizeof(*store));

   if (!store) {
      return NULL;
   }

   store->prefix_size = prefix_size ? prefix_size : DEFAULT_PREFIX_SIZE;
   store->prefix_index = calloc(store->prefix_size, sizeof(kv_list_t));

   if (!store->prefix_index) {
      free(store);
      return NULL;
   }

   store->type = type;

   for (size_t i = 0; i < store->prefix_size; i++) {
      store->prefix_index[i].type = type;
   }

   return store;
}

void kv_destroy(kv_store_t *store) {
   if (!store) {
      return;
   }

   for (size_t i = 0; i < store->prefix_size; i++) {
      kv_list_t *list = &store->prefix_index[i];

      if (list->type == KV_ARRAY) {
         kv_node_t **arr = (kv_node_t**)list->ptr;

         for (size_t j = 0; j < list->count; j++) {
            free(arr[j]->key);
            free(arr[j]);
         }

         free(arr);
      } else {
         bst_free((kv_node_t*)list->ptr);
      }
   }

   free(store->prefix_index);
   free(store);
}

int kv_insert(kv_store_t *store, const char *key, void *val) {
   if (!store || !key || !key[0] || !key[1]) {
      return -1;
   }

   uint16_t idx = prefix_index_key(key);

   if (idx >= store->prefix_size) {
      return -1;
   }

   kv_list_t *list = &store->prefix_index[idx];
   const char *suffix = key + 2;

   if (list->type == KV_ARRAY) {
      kv_node_t **arr = (kv_node_t**)list->ptr;
      int pos = kv_array_bsearch(arr, list->count, suffix);

      if (pos >= 0) {
         arr[pos]->value = val;
         return 0;
      }

      pos = -pos - 1;

      if (list->count == list->cap) {
         size_t newcap = list->cap ? list->cap * 2 : 4;
         kv_node_t **newarr = realloc(arr, newcap * sizeof(kv_node_t*));

         if (!newarr) {
            return -1;
         }

         arr = newarr;
         list->cap = newcap;
      }

      memmove(&arr[pos + 1], &arr[pos], (list->count - pos) * sizeof(kv_node_t*));

      kv_node_t *node = calloc(1, sizeof(*node));
      node->key = strdup(suffix);
      node->value = val;
      arr[pos] = node;
      list->count++;
      list->ptr = arr;

      return 0;
   } else {
      kv_node_t *root = (kv_node_t*)list->ptr;
      root = bst_insert(root, suffix, val);
      list->ptr = root;
      return 0;
   }
}

void *kv_lookup(kv_store_t *store, const char *key) {
   if (!store || !key || !key[0] || !key[1]) {
      return NULL;
   }

   uint16_t idx = prefix_index_key(key);

   if (idx >= store->prefix_size) {
      return NULL;
   }

   kv_list_t *list = &store->prefix_index[idx];
   const char *suffix = key + 2;

   if (list->type == KV_ARRAY) {
      kv_node_t **arr = (kv_node_t**)list->ptr;
      int pos = kv_array_bsearch(arr, list->count, suffix);

      if (pos >= 0) {
         return arr[pos]->value;
      }

      return NULL;
   } else {
      return bst_lookup((kv_node_t*)list->ptr, suffix);
   }
}

int kv_remove(kv_store_t *store, const char *key) {
   if (!store || !key || !key[0] || !key[1]) {
      return -1;
   }

   uint16_t idx = prefix_index_key(key);

   if (idx >= store->prefix_size) {
      return -1;
   }

   kv_list_t *list = &store->prefix_index[idx];
   const char *suffix = key + 2;

   if (list->type == KV_ARRAY) {
      kv_node_t **arr = (kv_node_t**)list->ptr;
      int pos = kv_array_bsearch(arr, list->count, suffix);

      if (pos < 0) {
         return -1;
      }

      free(arr[pos]->key);
      free(arr[pos]);
      memmove(&arr[pos], &arr[pos + 1], (list->count - pos - 1) * sizeof(kv_node_t*));
      list->count--;
      list->ptr = arr;

      return 0;
   } else {
      int removed = 0;
      kv_node_t *root = (kv_node_t*)list->ptr;
      root = bst_remove(root, suffix, &removed);
      list->ptr = root;

      if (removed) {
         return 0;
      }

      return -1;
   }
}

kv_store_t *kv_create_and_load(kv_type_t type, size_t prefix_size, ...) {
   kv_store_t *store = kv_create(prefix_size, type);

   if (!store) {
      return NULL;
   }

   va_list ap;
   va_start(ap, prefix_size);

   const char *key;

   while ((key = va_arg(ap, const char*)) != NULL) {
      void *val = va_arg(ap, void*);

      if (!val) {
         break;
      }

      kv_insert(store, key, val);
   }

   va_end(ap);
   return store;
}

#if	0
// ----------------- example -----------------
int main(void) {
   kv_store_t *store = kv_create_and_load(KV_BST, 0,
       "#a", "val1",
       "#b", "val2",
       "#cX", "val3",
       NULL
   );

   printf("#a -> %s\n", (char*)kv_lookup(store, "#a"));
   printf("#b -> %s\n", (char*)kv_lookup(store, "#b"));
   printf("#cX -> %s\n", (char*)kv_lookup(store, "#cX"));

   kv_remove(store, "#b");
   printf("#b -> %s\n", (char*)kv_lookup(store, "#b"));

   kv_destroy(store);
   return 0;
}
#endif
