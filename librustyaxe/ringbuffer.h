#if	!defined(_ringbuffer_h)
#define	_ringbuffer_h
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
   typedef struct rb_node {
      void *data;
      int  needs_freed;				// If this is set, data pointer is freed automatically
      struct timespec timestamp;
      struct rb_node *next;
   } rb_node_t;

   typedef struct rb_buffer {
      size_t max_size;
      size_t current_size;
      rb_node_t *head;
      rb_node_t *tail;
      char *name; 				// name of the buffer (for debugging)
   } rb_buffer_t;

   extern rb_buffer_t* rb_create(int max_size, const char *name);
   extern void rb_destroy(rb_buffer_t* buffer);
   extern rb_node_t *rb_add(rb_buffer_t* buffer, void* data, int needs_freed);
   extern rb_node_t *rb_get_most_recent(rb_buffer_t* buffer);
   extern void **rb_get_range(rb_buffer_t* buffer, int start, int count);
#ifdef __cplusplus
}
#endif

#endif	// !defined(_ringbuffer_h)
