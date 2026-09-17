#ifndef	__libfwdspmgr_fwdsp_ctl_h
#define	__libfwdspmgr_fwdsp_ctl_h

#endif	// !__libfwdspmgr_fwdsp_ctl_h

extern bool fwdsp_cmd_shutdown(const char codec_id[5], bool is_tx, int unused1);
extern bool fwdsp_cmd_setvol(const char codec_id[5], bool is_tx, int percent);
