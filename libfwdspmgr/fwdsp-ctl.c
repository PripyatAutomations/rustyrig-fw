//
// libfwdspmgr/fwdsp-ctl.c: control messages support for fwdsp
//
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

bool fwdsp_cmd_setvol(const char codec_id[5], bool is_tx, int percent) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(codec_id, is_tx);
   struct fwdsp_control_msg msg = {
      .magic = FWDSP_CTRL_MAGIC,
      .type = FWDSP_CTRL_SET_VOLUME,
      .value = (uint8_t)(percent < 0 ? 0 : percent > 100 ? 100 : percent)
   };

   if (!sp || sp->fw_control <= 0) {
      return true;
   }
   return write(sp->fw_control, &msg, sizeof(msg)) == (ssize_t)sizeof(msg) ? false : true;
}

bool fwdsp_cmd_shutdown(const char codec_id[5], bool is_tx, int unused1) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(codec_id, is_tx);
   struct fwdsp_control_msg msg = {
      .magic = FWDSP_CTRL_MAGIC,
      .type = FWDSP_CTRL_SHUTDOWN,
      .value = (uint8_t)1
   };

   if (!sp || sp->fw_control <= 0) {
      return true;
   }
   return write(sp->fw_control, &msg, sizeof(msg)) == (ssize_t)sizeof(msg) ? false : true;
}


static bool fwdsp_cmd_record(const char codec_id[5], bool is_tx,
   const char *channel_uuid, bool start) {
   struct fwdsp_subproc *sp = NULL;

   if (channel_uuid && *channel_uuid) {
      sp = fwdsp_find_channel_instance(codec_id, is_tx, channel_uuid);
   } else {
      sp = fwdsp_find_instance(codec_id, is_tx);
   }

   struct fwdsp_control_msg msg = {
      .magic = FWDSP_CTRL_MAGIC,
      .type = start ? FWDSP_CTRL_START_RECORD : FWDSP_CTRL_STOP_RECORD,
      .value = start ? 1 : 0
   };

   if (!sp || sp->fw_control <= 0) {
      return true;
   }

   return write(sp->fw_control, &msg, sizeof(msg)) == (ssize_t)sizeof(msg) ? false : true;
}

bool fwdsp_cmd_start_record_channel(const char codec_id[5], bool is_tx,
   const char *channel_uuid) {
   return fwdsp_cmd_record(codec_id, is_tx, channel_uuid, true);
}

bool fwdsp_cmd_stop_record_channel(const char codec_id[5], bool is_tx,
   const char *channel_uuid) {
   return fwdsp_cmd_record(codec_id, is_tx, channel_uuid, false);
}

bool fwdsp_cmd_start_record(const char codec_id[5], bool is_tx, int unused1) {
   (void)unused1;
   return fwdsp_cmd_start_record_channel(codec_id, is_tx, NULL);
}

bool fwdsp_cmd_stop_record(const char codec_id[5], bool is_tx, int unused1) {
   (void)unused1;
   return fwdsp_cmd_stop_record_channel(codec_id, is_tx, NULL);
}
