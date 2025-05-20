if (!window.webui_inits) window.webui_inits = [];

window.webui_inits.push(function webui_audio_init() {
   $('button#use-audio').click(webui_audio_start);

   // Wait for socket to exist before allowing audio init
   if (!window.socket) {
      console.warn("Audio init delayed: socket not yet available");
      console.log("click start audio button in rig tab to start audio");
      return;
   }
});

async function webui_audio_start() {
   const socket = window.socket;
   if (!socket || socket.readyState !== WebSocket.OPEN) {
      console.error("webui_au_init: socket not ready");
      return;
   }

   $('button#use-audio').addClass('green-btn');
   let captureSeq = 0;

   const audioContext = new (window.AudioContext || window.webkitAudioContext)();

   await audioContext.audioWorklet.addModule('js/microphone-processor.js');
   await audioContext.audioWorklet.addModule('js/speaker-processor.js');

   // 🎙️ Microphone
   const micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
   const micSource = audioContext.createMediaStreamSource(micStream);
   const micWorkletNode = new AudioWorkletNode(audioContext, 'microphone-processor');

   micWorkletNode.port.onmessage = (event) => {
      const float32Array = event.data;
      const buffer = new ArrayBuffer(float32Array.length * 2 + 4);
      const view = new DataView(buffer);
      view.setUint32(0, captureSeq++, true);

      for (let i = 0; i < float32Array.length; i++) {
         const s = Math.max(-1, Math.min(1, float32Array[i]));
         view.setInt16(4 + i * 2, s * 0x7FFF, true);
      }

      if (socket.readyState === WebSocket.OPEN) {
         socket.send(buffer);
      }
   };

   micSource.connect(micWorkletNode);
   micWorkletNode.connect(audioContext.destination); // optional monitoring

   // 🔊 Speaker
   const speakerNode = new AudioWorkletNode(audioContext, 'speaker-processor');
   speakerNode.connect(audioContext.destination);

   socket.addEventListener('message', (event) => {
      const data = event.data;
      if (!(data instanceof ArrayBuffer)) return;

      const view = new DataView(data);
      const seq = view.getUint32(0, true);
      const int16Samples = new Int16Array(data.slice(4));
      const float32Samples = new Float32Array(int16Samples.length);

      for (let i = 0; i < int16Samples.length; i++) {
         float32Samples[i] = int16Samples[i] / 0x7FFF;
      }

      speakerNode.port.postMessage(float32Samples);
   });

   // Optional stop hook
   function stopAudio() {
      micWorkletNode.disconnect();
      micSource.disconnect();
      speakerNode.disconnect();
      micStream.getTracks().forEach(track => track.stop());
      audioContext.close();
   }

   // You can expose stopAudio or bind it to a UI button if needed
   $('button#use-audio').off('click');
   $('button#use-audio').click(function() {
      stopAudio();
      $('button#use-audio').removeClass('green-btn');
      $('button#use-audio').off('click');
      $('button#use-audio').click(webui_audio_start);
   });
}
