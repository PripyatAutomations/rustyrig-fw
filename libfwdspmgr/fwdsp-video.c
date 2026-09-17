//
// libfwdsp/fwpdsp-mgr.c: Deal with starting and stopping fwdsp instances as needed
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// codec negotiation should call fwdsp_create
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>
// Start (or ref up) a video pipeline (e.g. webcam capture) for a codec magic.
// Like fwdsp_codec_start, but the subprocess is spawned with -v so fwdsp
// treats it as a video stream. Returns the channel id, or -1 on failure.
int fwdsp_video_start(const char codec_id[5], bool is_tx) {
   if (!codec_id || codec_id[0] == '\0') {
      return -1;
   }

   if (fwdsp_init() ) {
      Log(LOG_CRIT, "fwdsp", "fwdsp_video_start: mgr init failed");
      return -1;
   }

   struct fwdsp_subproc *sp = fwdsp_find_instance(codec_id, !is_tx);
   if (!sp) {
      // create + spawn, marking the instance as video before exec
      sp = fwdsp_find_or_create(codec_id, FW_IO_STDIO, is_tx);
      if (!sp) {
         Log(LOG_CRIT, "fwdsp", "fwdsp_video_start: failed to create %s", codec_id);
         return -1;
      }

      // The instance may already be running (audio use of the same id?);
      // only spawn here when it has no pid yet.
      if (!sp->pid) {
         sp->is_video = true;
         if (!fwdsp_spawn(sp) ) {
            Log(LOG_CRIT, "fwdsp", "fwdsp_video_start: spawn failed for %s", codec_id);
            return -1;
         }
      }
   }
   sp->refcount++;
   return sp->chan_id;
}
