#ifndef FWDSP_PCM_BACKLOG_H
#define FWDSP_PCM_BACKLOG_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Latest 100 ms of canonical mono S16LE/16 kHz. Preserve encoded streams;
 * discard stale decoded sound only, without assuming codec packet duration. */
#define FWDSP_PCM_BACKLOG_BYTES 3200
struct fwdsp_pcm_backlog { uint8_t data[FWDSP_PCM_BACKLOG_BYTES]; size_t len; };
static inline void fwdsp_pcm_backlog_append(struct fwdsp_pcm_backlog *queue, const void *data, size_t len) {
   if (!data || !len || (len & 1)) return;
   const uint8_t *bytes = data;
   if (len >= sizeof(queue->data)) {
      memcpy(queue->data, bytes + len - sizeof(queue->data), sizeof(queue->data));
      queue->len = sizeof(queue->data);
      return;
   }
   if (queue->len + len > sizeof(queue->data)) {
      size_t discard = queue->len + len - sizeof(queue->data);
      memmove(queue->data, queue->data + discard, queue->len - discard);
      queue->len -= discard;
   }
   memcpy(queue->data + queue->len, bytes, len);
   queue->len += len;
}
#endif
