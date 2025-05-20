if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_audio_init() {
   // initialize audio
   window.socket = socket;
   webui_au_init();
   // In case browser blocks automatically starting the audioContext
   $('#start-audio').click(webui_au_init);
});

/// feel free to dump this and start over, but the above needs to remain to delay start until module is loaded by index.html
function webui_au_init() {
      $(async function () {
          let captureSeq = 0;
          const socket = window.socket;

          const audioContext = new (window.AudioContext || window.webkitAudioContext)();

          // Load both processors
          await audioContext.audioWorklet.addModule('js/microphone-processor.js');
          await audioContext.audioWorklet.addModule('js/speaker-processor.js');

          // ===== 🎙️ Microphone (Capture to Server) =====
          const micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
          const micSource = audioContext.createMediaStreamSource(micStream);
          const micWorkletNode = new AudioWorkletNode(audioContext, 'microphone-processor');

          micWorkletNode.port.onmessage = (event) => {
              const float32Array = event.data;
              const buffer = new ArrayBuffer(float32Array.length * 2 + 4); // 4 bytes for seq
              const view = new DataView(buffer);

              // Sequence number (Uint32)
              view.setUint32(0, captureSeq++, true);

              // Convert Float32 → Int16 PCM
              for (let i = 0; i < float32Array.length; i++) {
                  const s = Math.max(-1, Math.min(1, float32Array[i]));
                  view.setInt16(4 + i * 2, s * 0x7FFF, true);
              }

              if (socket && socket.readyState === WebSocket.OPEN) {
                  socket.send(buffer);
              }
          };

          micSource.connect(micWorkletNode);
          micWorkletNode.connect(audioContext.destination); // Optional (can mute)

          // ===== 🔊 Speaker (Server to Playback) =====
          const speakerNode = new AudioWorkletNode(audioContext, 'speaker-processor');

          speakerNode.connect(audioContext.destination);

          // Convert incoming Int16 PCM from server to Float32 and post to speaker
          socket.addEventListener('message', (event) => {
              const data = event.data;
              if (!(data instanceof ArrayBuffer)) return;

              const view = new DataView(data);

              const seq = view.getUint32(0, true); // optionally use this
              const int16Samples = new Int16Array(data.slice(4));
              const float32Samples = new Float32Array(int16Samples.length);

              for (let i = 0; i < int16Samples.length; i++) {
                  float32Samples[i] = int16Samples[i] / 0x7FFF;
              }

              speakerNode.port.postMessage(float32Samples);
          });

          // ===== 🛑 Optional: Stop Function =====
          function stopAudio() {
              micWorkletNode.disconnect();
              micSource.disconnect();
              speakerNode.disconnect();
              micStream.getTracks().forEach(track => track.stop());
              audioContext.close();
          }

          // Hook to a UI element if desired
          // $('#stopBtn').on('click', stopAudio);
      });
}

