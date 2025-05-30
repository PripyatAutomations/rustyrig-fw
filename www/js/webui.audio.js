var Flac;
if (typeof WebAssembly === 'object' && WebAssembly){
   //load wasm-based library
   Flac = require('libflac.min.wasm.js');
   //or, for example, in worker script: importScripts('libflac.min.wasm.js');
} else {
   //load asm.js-based library
   Flac = require('libflac.min.js');
   //or, for example, in worker script: importScripts('libflac.min.js');
}
const WebUiAudio = {
   rx_context: null,
   tx_context: null,
   micStream: null,
   txNode: null,
   rxNode: null,
   captureSeq: 0,

   async start() {
      if (!window.socket || socket.readyState !== WebSocket.OPEN) {
         console.error("WebUiAudio: socket not ready");
         return;
      }

      socket.binaryType = 'arraybuffer';

      if (this.rx_context) {
         console.warn("WebUiAudio already running");
         return;
      }

      this.rx_context = new AudioContext({ sampleRate: 44100 });
      this.tx_context = new AudioContext({ sampleRate: 44100 });
      await this.tx_context.audioWorklet.addModule('js/tx-processor.js');
      await this.rx_context.audioWorklet.addModule('js/rx-processor.js');

      this.micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
      const micSource = this.tx_context.createMediaStreamSource(this.micStream);
      this.txNode = new AudioWorkletNode(this.tx_context, 'tx-processor');

      this.txNode.port.onmessage = (event) => {
         const float32Array = event.data;
         const buffer = new ArrayBuffer(float32Array.length * 2 + 4);
         const view = new DataView(buffer);
         view.setUint32(0, this.captureSeq++, true);
         for (let i = 0; i < float32Array.length; i++) {
            const s = Math.max(-1, Math.min(1, float32Array[i]));
            view.setInt16(4 + i * 2, s * 0x7FFF, true);
         }
         if (socket.readyState === WebSocket.OPEN)
            socket.send(buffer);
      };
      micSource.connect(this.txNode);

      this.rxNode = new AudioWorkletNode(this.rx_context, 'rx-processor');
      this.rxGainNode = this.rx_context.createGain();
      this.rxGainNode.gain.value = parseFloat($('#rig-rx-vol').val());
      this.rxNode.connect(this.rxGainNode);
      this.rxGainNode.connect(this.rx_context.destination);

      this.txGainNode = this.tx_context.createGain();
      this.txGainNode.gain.value = parseFloat($('#rig-tx-vol').val());
      this.txNode.connect(this.txGainNode);
      this.txGainNode.connect(this.tx_context.destination); // monitor

      socket.addEventListener('message', this._onSocketMessage);
   },

   _onSocketMessage(event) {
      if (!(event.data instanceof ArrayBuffer)) return;
      const view = new DataView(event.data);
      const int16Samples = new Int16Array(event.data.slice(4));
      const float32Samples = new Float32Array(int16Samples.length);
      for (let i = 0; i < int16Samples.length; i++) {
         float32Samples[i] = int16Samples[i] / 0x7FFF;
      }
      WebUiAudio.rxNode.port.postMessage(float32Samples);
   },

   stop() {
      if (this.rx_context) {
         this.rxNode?.disconnect();
         this.rxGainNode?.disconnect();
         this.rx_context.close();
         this.rx_context = null;
         console.log(" => Stopped RX");
      }

      if (this.tx_context) {
         this.txNode?.disconnect();
         this.txGainNode?.disconnect();
         this.micStream?.getTracks().forEach(t => t.stop());
         this.tx_context.close();
         this.tx_context = null;
         console.log(" => Stopped TX");
      }
      socket.removeEventListener('message', this._onSocketMessage);
   },

   handle_binary_frame(event) {
      WebUiAudio.onBinaryFrame(event.data);
   },

   flushPlayback() {
      if (this.rxNode?.port) {
         this.rxNode.port.postMessage({ flush: true });
      }
   },

   onBinaryFrame(data) {
      if (!this.rxNode) return;

      const view = new DataView(data);
      const int16Samples = new Int16Array(data.slice(4));
      const float32Samples = new Float32Array(int16Samples.length);
      for (let i = 0; i < int16Samples.length; i++) {
         float32Samples[i] = int16Samples[i] / 0x7FFF;
      }
      this.rxNode.port.postMessage(float32Samples);
   }
};

// Hook up button and sliders
$('button#start-audio').click(async function () {
   if (!WebUiAudio.rx_context) {
      await WebUiAudio.start();
      console.log("Started audio streaming");
      $(this).addClass('green-btn');
   } else {
      WebUiAudio.stop();
      console.log("Stopped audio streaming");
      $(this).removeClass('green-btn');
   }
});

$('#rig-rx-vol').change(function() {
   if (WebUiAudio.rxGainNode) {
      WebUiAudio.rxGainNode.gain.value = parseFloat($(this).val());
   }
});

$('#rig-tx-vol').change(function() {
   if (WebUiAudio.txGainNode) {
      WebUiAudio.txGainNode.gain.value = parseFloat($(this).val());
   }
});

if (!window.webui_inits) window.webui_inits = [];

window.webui_inits.push(function webui_audio_init() {
   $('button#start-audio').click(WebUiAudio.start());
});
