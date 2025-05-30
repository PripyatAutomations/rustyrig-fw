
#define DEFAULT_SOCKET_PATH_TX "./run/rustyrig_tx.sock"
#define DEFAULT_SOCKET_PATH_RX "./run/rustyrig_rx.pipe"

struct audio_header {
   uint8_t magic[2];      // e.g. "AU"
   uint8_t sample_rate;   // 0 = 16000, 1 = 44100
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
