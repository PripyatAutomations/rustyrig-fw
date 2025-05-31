//
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
/*
            this.audioContext.audioWorklet.addModule('js/audio.worklet.alert.js').then(() => {
               this.WorkletNode = new AudioWorkletNode(this.audioContext, 'tx-processor');
               this.WorkletNode.connect(this.gainNode);
               this.gainNode.connect(this.audioContext.destination);
            }).catch((err) => {
               console.error('[audio] Failed to load tx-processor:', err);
            });
 */
            // XXX: Add support for sending alerts to a different sound device
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

class WebUiAudio {
  constructor() {
     this.channels = {};

     // Attach UI elements to this element
     this.AttachVolumeSliders();

     // RX Audio
     this.channels.rx = new WebUiAudioDev('rx', 44100, 1.0);

     // TX Audio
     this.channels.tx = new WebUiAudioDev('tx', 44100, 1.0);
     console.log('[audio] Subsystem ready.');
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

//function WebUiAudioAttach(WebUiAudio) {
//}

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
