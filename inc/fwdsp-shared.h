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

struct audio_header {
   uint8_t magic[2];      // e.g. "AU"
   uint32_t sample_rate;
   uint8_t format;        // 0 = S16LE, 1 = FLAC, 2 = OPUS
   uint8_t channel_id;	  // Channel ID: 0 for RX, 1 for TX, etc
}  __attribute__((packed));

struct audio_config {
   const char *template;
   const char *sock_path;
   int sample_rate;   // e.g. 16000 or 44100
   int format;        // 0 = S16LE, 1 = FLAC, 2 = OPUS
   bool tx_mode;
   bool persistent;   // persistent mode, keep reconnecting until fatal signal
   int channel_id;
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
