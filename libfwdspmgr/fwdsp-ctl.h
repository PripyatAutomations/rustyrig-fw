#ifndef __libfwdspmgr_fwdsp_ctl_h
#define __libfwdspmgr_fwdsp_ctl_h

#include <stdbool.h>

extern bool fwdsp_cmd_shutdown(const char codec_id[5], bool is_tx, int unused1);
extern bool fwdsp_cmd_setvol(const char codec_id[5], bool is_tx, int percent);
extern bool fwdsp_cmd_start_record(const char codec_id[5], bool is_tx, int unused1);
extern bool fwdsp_cmd_stop_record(const char codec_id[5], bool is_tx, int unused1);
extern bool fwdsp_cmd_start_record_channel(const char codec_id[5], bool is_tx,
   const char *channel_uuid);
extern bool fwdsp_cmd_stop_record_channel(const char codec_id[5], bool is_tx,
   const char *channel_uuid);
extern bool fwdsp_cmd_start_record_named(const char codec_id[5], bool is_tx,
   const char *channel_uuid, const char *username, bool record_tx);

#endif // !__libfwdspmgr_fwdsp_ctl_h
