//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef __libfwdspmgr_fwdsp_ctl_h
#define __libfwdspmgr_fwdsp_ctl_h

#include <stdbool.h>

struct fwdsp_subproc;
extern bool fwdsp_set_quality_hint(struct fwdsp_subproc *sp, unsigned percent);
extern bool fwdsp_cmd_shutdown(const char codec_id[5], bool is_tx, int unused1);
extern bool fwdsp_cmd_setvol(const char codec_id[5], bool is_tx, int percent);
extern bool fwdsp_cmd_start_record(const char codec_id[5], bool is_tx, int unused1);
extern bool fwdsp_cmd_stop_record(const char codec_id[5], bool is_tx, int unused1);
extern bool fwdsp_cmd_start_record_channel(const char codec_id[5], bool is_tx, const char *channel_uuid);
extern bool fwdsp_cmd_stop_record_channel(const char codec_id[5], bool is_tx, const char *channel_uuid);
extern bool fwdsp_cmd_start_record_named(const char codec_id[5], bool is_tx, const char *channel_uuid, const char *username, bool record_tx);
extern bool fwdsp_cmd_start_record_named_id(const char codec_id[5], bool is_tx, const char *channel_uuid, const char *username, bool record_tx, const char *
   recording_id);
extern bool fwdsp_cmd_start_record_named_file(const char codec_id[5], bool is_tx, const char *channel_uuid, const char *username, bool record_tx, const char *
   recording_id, const char *record_file);

#endif // !__libfwdspmgr_fwdsp_ctl_h
