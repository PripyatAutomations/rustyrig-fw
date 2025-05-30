if (!window.webui_inits) window.webui_inits = [];

const audioCtx = new AudioContext({ sampleRate: 16000 });
// Support volume control
const gainNode = audioCtx.createGain();
gainNode.gain.value = 1.0;
gainNode.connect(audioCtx.destination);

let playbackTime = audioCtx.currentTime;

// Halt playback
function stopPlayback() {
   playbackTime = audioCtx.currentTime;
}

// Flush the playback buffer then insert silence
function flushPlayback() {
   const sampleRate = audioCtx.sampleRate;
   const silence = new Float32Array(sampleRate / 10); // 100ms silence

   const audioBuffer = audioCtx.createBuffer(1, silence.length, sampleRate);
   audioBuffer.copyToChannel(silence, 0);

   const source = audioCtx.createBufferSource();
   source.buffer = audioBuffer;
   source.connect(audioCtx.destination);

   const now = audioCtx.currentTime;
   if (playbackTime < now) {
      playbackTime = now;
   }

   source.start(playbackTime);
   playbackTime += silence.length / sampleRate;
}

function playRawPCM(buffer) {
   const pcmData = new Int16Array(buffer);
   const float32Data = new Float32Array(pcmData.length);

   for (let i = 0; i < pcmData.length; i++) {
      float32Data[i] = pcmData[i] / 32768;
   }

   const samplesPerPacket = float32Data.length;
   const sampleRate = audioCtx.sampleRate;  // might be 44100, not 16000
   const duration = samplesPerPacket / sampleRate;

   const audioBuffer = audioCtx.createBuffer(1, samplesPerPacket, sampleRate);
   audioBuffer.copyToChannel(float32Data, 0);

   const source = audioCtx.createBufferSource();
   source.buffer = audioBuffer;
   source.connect(audioCtx.destination);

   // Schedule the audio to play in sequence
   const now = audioCtx.currentTime;
   if (playbackTime < now) {
      playbackTime = now;
   }

   source.start(playbackTime);
   playbackTime += duration;
}

$('#rig-playback-vol').on('input', function () {
   gainNode.gain.value = parseFloat($(this).val());
});

window.webui_inits.push(function webui_audio_init() {
//   $('button#use-audio').click(webui_audio_start);

   // Wait for socket to exist before allowing audio init
   if (!window.socket) {
      console.warn("Audio init delayed: socket not yet available");
      console.log("click start audio button in rig tab to start audio");
      return;
   }
});
