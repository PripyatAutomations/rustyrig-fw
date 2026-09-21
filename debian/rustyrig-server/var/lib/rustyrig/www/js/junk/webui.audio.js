/////////////
// globals //
/////////////
var Audio;
var WebUiFlacDecoder;
var WebUiFlacEncoder;
var audio_active;

// Audio Device wrapper for RX, TX, and ALERT channels
class WebUiAudioDev {
   constructor(ChannelType, sampleRate, gain) {
      try {
         this.audioContext = new AudioContext({ sampleRate: sampleRate });
         this.gainNode = this.audioContext.createGain();

         // If a valid value provided, set the gain
         if (typeof gain !== 'undefined' && gain >= 0) {
            this.gainNode.gain.value = gain;
         }

         if (ChannelType === 'rx') {
            this.audioContext.audioWorklet.addModule('js/audio.worklet.rx.js').then(() => {
               this.WorkletNode = new AudioWorkletNode(this.audioContext, 'rx-processor');
               this.WorkletNode.connect(this.gainNode);
               this.gainNode.connect(this.audioContext.destination);
               this.audioContext.resume();
            }).catch((err) => {
               console.error('[audio] Failed to load rx-processor:', err);
            });
         } else if (ChannelType === 'tx') {
            this.audioContext.audioWorklet.addModule('js/audio.worklet.tx.js').then(() => {
               this.WorkletNode = new AudioWorkletNode(this.audioContext, 'tx-processor');
               this.WorkletNode.connect(this.gainNode);
               this.gainNode.connect(this.audioContext.destination);
            }).catch((err) => {
               console.error('[audio] Failed to load tx-processor:', err);
            });
         } else if (ChannelType === 'alert') {
            // XXX: Add support for sending alerts to a different sound device
/*
            this.audioContext.audioWorklet.addModule('js/audio.worklet.alert.js').then(() => {
               this.WorkletNode = new AudioWorkletNode(this.audioContext, 'alert-processor');
               this.WorkletNode.connect(this.gainNode);
               this.gainNode.connect(this.audioContext.destination);
            }).catch((err) => {
               console.error('[audio] Failed to load alert-processor:', err);
            });
 */
         }
      } catch(e) {
         console.log('Error:', e);
         return null;
      }
   }

   SetGain(gain) {
      this.gainNode.gain.value = gain;
   }

   GetGain() {
       return this.gainNode.gain.value;
   }
}

class WebUiAudioCodec {
   constructor() {
      this.inputBuffer = [];
      this.inputOffset = 0;
   }

   StartFlacDecoder() {
      let decoder;

      decoder = Flac.create_libflac_decoder();

      this.flac_dec_write_callback = this.flac_dec_write_callback.bind(this);
      if (!decoder) {
          console.error("[flac] Could not create FLAC decoder.");
          return;
      }

      let rv = Flac.init_decoder_ogg_stream(
          decoder,
          this.flac_dec_read_callback,
          this.flac_dec_write_callback,
          this.flac_dec_metadata_callback,
          this.flac_dec_error_callback,
          0 // client_data
      );

      if (rv != 0) {
         console.error("[flac] decoder init failed: ", rv);
      }

      this.flac_decoder = decoder;
   }

   StartFlacEncoder() {
      let encoder;
      encoder = Flac.create_libflac_encoder();
      if (!encoder) {
         console.error("[flac] Could not create FLAC encoder.");
         return;
      }
      let rv = Flac.init_encoder_ogg_stream(encoder, this.flac_enc_write_callback);
      if (rv != 0) {
         console.error("[flac] encoder init failed: ", rv);
      }
   }

   flac_dec_read_callback(buffer, bytes) {
      const target = new Uint8Array(Flac.HEAPU8.buffer, buffer, bytes);
      let readLen = Math.min(bytes, this.inputBuffer.length - this.inputOffset);
      for (let i = 0; i < readLen; i++) {
         target[i] = this.inputBuffer[this.inputOffset++];
      }

      // If we've consumed all input, reset buffer
      if (this.inputOffset >= this.inputBuffer.length) {
         this.inputBuffer = [];
         this.inputOffset = 0;
      }

      return readLen;
   }

   flac_dec_write_callback(buffer, numChannels, numSamples, sampleRate, bitsPerSample, totalSamples) {
      if (!this.rxNode || !this.rxNode.port) return;

      // We're assuming signed 16-bit PCM data in buffer
      const floatBuffer = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
         floatBuffer[i] = buffer[i] / 32768.0;
      }

      this.rxNode.port.postMessage(floatBuffer);
   }

   flac_decode_chunk(chunk) {
      Flac.decode_ogg_chunk(this.flac_decoder, new Uint8Array(chunk));
   }

   flac_dec_error_callback() {
   }

   flac_dec_metadata_callback() {
   }

   flac_enc_read_callback() {
   }
   
   flac_enc_write_callback() {
   }
}

class WebUiAudio {
  constructor() {
     this.channels = {};

     // Attach UI elements to this element
     this.AttachVolumeSliders();

     // RX Audio
     this.channels.rx = new WebUiAudioDev('rx', 44100, 1.0);
/*
     // TX Audio
     this.channels.tx = new WebUiAudioDev('tx', 44100, 1.0);
     console.log('[audio] Subsystem ready.');
 */
  }

  StartPlayback() {
  }

  StopPlayback() {
  }

  AttachVolumeSliders() {
     $('#rig-rx-vol').on('change', (event) => {
        const val = parseFloat($(event.target).val());
        if (this.channels.rx) {
           this.channels.rx.SetGain(val);
        }
     });

     $('#rig-tx-vol').on('change', (event) => {
        const val = parseFloat($(event.target).val());
        if (this.channels.tx) {
           this.channels.tx.SetGain(val);
        }
     });
  }
}


function handle_binary_frame(arrayBuffer) {
   const chunk = new Uint8Array(arrayBuffer);
   if (audio_active) {
/*
      WebUiFlacDecoder.inputBuffer.push(...chunk);
      Flac.decode_ogg_stream(WebUiFlacDecoder.flac_decoder);
*/
     rx_worker.postMessage({
          type: 'ogg_chunk',
          data: arrayBuffer
     }, [arrayBuffer]);  // Transfer ownership to avoid copying
     WebUiFlacDecoder.decode_chunk(arrayBuffer);
   }
}

function webui_audio_start() {
   Audio = new WebUiAudio();
   WebUiFlacDecoder = new WebUiAudioCodec();
   WebUiFlacDecoder.StartFlacDecoder();
//   WebUiFlacEncoder = new WebUiAudioCodec();
//   WebUiFlacEncoder.StartFlacEncoder();
   audio_active = true;
}

if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_audio_init() {
   console.log('[audio] Initializing subsystem');

   Flac.onready = function() {
     console.log('[audio] FLAC WASM loaded');
   };

   Flac.onerror = function(err) {
     console.error('[audio] FLAC error:', err);
   };

   console.log('[audio] FLAC available?', typeof Flac !== 'undefined');
});


var rx_worker = null;

async function setupAudio() {
  const context = new AudioContext({ sampleRate: 16000 });
  await context.audioWorklet.addModule('/js/audio.worklet.pcm-player.js');

  const rxNode = new AudioWorkletNode(context, 'pcm-player');
  rxNode.connect(context.destination);

  // expose to your system
  const Audio = window.Audio || {};
  Audio.channels = Audio.channels || {};
  Audio.channels.rx = Audio.channels.rx || {};
  Audio.channels.rx.WorkletNode = rxNode;
  this.rxNode = rxNode;

  rx_worker = new Worker('/js/decoder.worker.flac.js');
  rx_worker.postMessage({ type: 'init' });

  rx_worker.onmessage = (e) => {
    if (e.data.type === 'pcm') {
      rxNode.port.postMessage(e.data); // forward to AudioWorklet
    }
  };
}
