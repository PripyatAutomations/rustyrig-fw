#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <librustyaxe/core.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>
int main(void) {
   int sockets[2];
   assert(!socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets));
   struct fwdsp_subproc child = {.fw_control = sockets[0]};
   assert(fwdsp_set_quality_hint(NULL, 50));
   assert(fwdsp_set_quality_hint(&child, 0));
   assert(fwdsp_set_quality_hint(&child, 101));
   assert(!fwdsp_set_quality_hint(&child, 50));
   struct fwdsp_control_msg message;
   assert(recv(sockets[1], &message, sizeof(message), 0) == sizeof(message));
   assert(message.magic == FWDSP_CTRL_MAGIC && message.type == FWDSP_CTRL_SET_QUALITY && message.value == 50);
   assert(!fwdsp_set_quality_hint(&child, 50));
   assert(recv(sockets[1], &message, sizeof(message), MSG_DONTWAIT) < 0);
   close(sockets[1]);
   assert(fwdsp_set_quality_hint(&child, 100));
   assert(child.quality_hint == 50); // failed hint is retryable
   close(sockets[0]);
   puts("PASS: quality IPC bounds, deduplication and retryable send failure");
   return 0;
}
