//
// fwdsp-shared.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#if	!defined(__fwdsp_shared_h)
#define	__fwdsp_shared_h

#define DEFAULT_SOCKET_PATH_TX "./state/fwdso-tx.sock"
#define DEFAULT_SOCKET_PATH_RX "./state/fwdsp-rx.pipe"

struct audio_config {
   const char *pipeline;
   const char *sock_path;
   char codec_id[5];	// codec ID + null
   int sample_rate;   // e.g. 16000 or 44100
   int format;        // 0 = S16LE, 1 = FLAC, 2 = OPUS
   bool tx_mode;
   bool persistent;   // persistent mode, keep reconnecting until fatal signal
   int channel_id;
   enum {
     FW_MEDIA_NONE = 0,
     FW_MEDIA_AUDIO,		// Audio source/sink
     FW_MEDIA_VIDEO		// Video (rig webcam?)
   } media_type;
   enum {
     FW_DIR_NONE = 0,
     FW_DIR_RX	,		// Media we are RECEIVING
     FW_DIR_TX			// Media we are SENDING
   } media_direction;
};

struct fwdsp_client {
   int  fd;                    // file descriptor for socket/pipe
   enum {
      FWDSP_CLI_NONE = 0,
      FWDSP_CLI_RX,            // RX audio path
      FWDSP_CLI_TX             // TX audio path
   } type;
};
typedef struct fwdsp_client fwdsp_client_t;

#endif	// !defined(__fwdsp_shared_h)
