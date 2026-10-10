#include <assert.h>
#include <stdio.h>
#include <libfwdspmgr/pcm-backlog.h>
int main(void) {
   struct fwdsp_pcm_backlog queue={0};
   uint8_t samples[8000];
   for (unsigned i=0;i<sizeof(samples);i++) samples[i]=i%251;
   fwdsp_pcm_backlog_append(&queue,samples,1000);
   fwdsp_pcm_backlog_append(&queue,samples+1000,1000);
   fwdsp_pcm_backlog_append(&queue,samples+2000,2000);
   assert(queue.len==3200 && !memcmp(queue.data,samples+800,3200));
   fwdsp_pcm_backlog_append(&queue,samples,8000);
   assert(queue.len==3200 && !memcmp(queue.data,samples+4800,3200));
   fwdsp_pcm_backlog_append(&queue,samples,1);
   assert(queue.len==3200);
   puts("PASS: codec-independent decoded PCM recovery retains the newest 100ms without splitting samples");
}
