/*
 * A reusable implementation of a ring buffer with timestamps for FIFO usage
 */
#include <librustyaxe/core.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

rb_buffer_t *rb_create(int max_size, const char *name) {
   rb_buffer_t *buffer = malloc(sizeof(rb_buffer_t));
   size_t name_len = strnlen(name, sizeof(buffer->name)) + 1; // add one for null terminator
   char *buffer_name = malloc(name_len);

   if (buffer == NULL || buffer_name == NULL) {
      fprintf(stderr, "rb_create: out of memory!\n");
      exit(ENOMEM);
   }

   buffer->head = NULL;
   buffer->tail = NULL;
   buffer->max_size = max_size;
   buffer->current_size = 0;
   strncpy(buffer_name, name, name_len);
   buffer_name[name_len - 1] = '\0'; // make sure name is null-terminated
   buffer->name = buffer_name;

   Log(LOG_DEBUG, "ringbuffer", "rb_create created new RingBuffer %s at %p", buffer->name, buffer);

   return buffer;
}

void rb_destroy(rb_buffer_t *buffer) {
   rb_node_t *current = buffer->head;

   while (current != NULL) {
      rb_node_t *next = current->next;
      Log(LOG_DEBUG, "ringbuffer", "rb: Destroying entry rb:%p (%s) to %p, needs_freed: %d", current, buffer->name, current->data, current->needs_freed);

      if (current->needs_freed && current->data != NULL) {
         free(current->data);
      }

      free(current);
      current = next;
   }
   free(buffer);
}

rb_node_t *rb_add(rb_buffer_t *buffer, void *data, int needs_freed) {
   struct timespec timestamp;
   clock_gettime(CLOCK_MONOTONIC, &timestamp);

   rb_node_t *node = malloc(sizeof(rb_node_t));
   
   if (node == NULL) {
      fprintf(stderr, "rb_add: out of memory!\n");
      exit(ENOMEM);
   }
   node->data = data;
   node->timestamp = timestamp;
   node->next = NULL;
   node->needs_freed = needs_freed;

   Log(LOG_DEBUG, "Adding entry %p to rb:%p (%s), needs_freed: %d", data, buffer, buffer->name, needs_freed);

   if (buffer->current_size == 0) {
       buffer->head = node;
       buffer->tail = node;
       buffer->current_size++;
       return NULL;
   }

   if (buffer->current_size == buffer->max_size) {
       rb_node_t *next_head = buffer->head->next;

       // before freeing the buffer structure, make sure free it's allocated memory
       if (buffer->head->needs_freed && buffer->head->data != NULL) {
          free(buffer->head->data);
       }

       free(buffer->head);
       buffer->head = next_head;
       buffer->current_size--;
   }

   // set ourselves onto the end of the list
   buffer->tail->next = node;

   // we are now the tail of the list
   buffer->tail = node;
   buffer->current_size++;
   return node;
}

rb_node_t *rb_get_most_recent(rb_buffer_t *buffer) {
   if (buffer == NULL) {
      Log(LOG_CRIT, "ringbuffer", "rb_get_most_recent: buffer == NUL!? ignoring request");
      return NULL;
   }

   if (buffer->current_size == 0) {
      Log(LOG_CRIT, "ringbuffer", "rb_get_most_recent: Ring buffer <%p> is empty.", buffer);
      return NULL;
   }

   rb_node_t *current = buffer->head;
   rb_node_t *latest_node = current;

   while (current != NULL) {
       if (current->timestamp.tv_sec > latest_node->timestamp.tv_sec ||
          (current->timestamp.tv_sec == latest_node->timestamp.tv_sec &&
           current->timestamp.tv_nsec > latest_node->timestamp.tv_nsec)) {
          latest_node = current;
       }
       current = current->next;
   }
   return latest_node;
}

void **rb_get_range(rb_buffer_t *buffer, int start, int count) {
   if (buffer->current_size == 0) {
      printf("Ring buffer is empty.\n");
      return NULL;
   }

   if (start < 0 || start >= buffer->current_size) {
      printf("Invalid start index.\n");
      return NULL;
   }

   if (count < 1 || start + count > buffer->current_size) {
      printf("Invalid count.\n");
      return NULL;
   }

   void **array = malloc(count  *sizeof(void*));

   if ((void *)array == NULL) {
      fprintf(stderr, "rb_get_range: out of memory!\n");
      exit(ENOMEM);
   }

   rb_node_t *current = buffer->head;

   int i = 0;

   while (i < start) {
      current = current->next;
      i++;
   }

   for (i = 0; i < count; i++) {
      array[i] = current->data;
      current = current->next;
   }

   return array;
}
