#if	!defined(__librustyaxe_kvstore_h)
#define __librustyaxe_kvstore_h

typedef enum { KV_ARRAY=0, KV_BST } kv_type_t;

typedef struct kv_node {
   char *key;
   void *value;
   struct kv_node *left;
   struct kv_node *right;
} kv_node_t;

typedef struct {
   void *ptr;        // array or BST root
   size_t count;
   size_t cap;
   kv_type_t type;
} kv_list_t;

typedef struct {
   kv_list_t *prefix_index;
   size_t prefix_size;
   kv_type_t type;
} kv_store_t;

#define DEFAULT_PREFIX_SIZE 65536

extern kv_store_t *kv_create(size_t prefix_size, kv_type_t type);
extern void kv_destroy(kv_store_t *store);
extern int kv_insert(kv_store_t *store, const char *key, void *val);
extern void *kv_lookup(kv_store_t *store, const char *key);
extern int kv_remove(kv_store_t *store, const char *key);
extern kv_store_t *kv_create_and_load(kv_type_t type, size_t prefix_size, ...);

#endif	// !defined(__librustyaxe_kvstore_h)
