if (!window.webui_inits) window.webui_inits = [];

// RX context
const rxCtx = new AudioContext({ sampleRate: 16000 });
let rxTime = rxCtx.currentTime;

// TX context
const txCtx = new AudioContext({ sampleRate: 16000 });
let txTime = txCtx.currentTime;

// Support volume control
const rxGainNode = rxCtx.createGain();
rxGainNode.gain.value = $('#rig-rx-vol').val();
rxGainNode.connect(rxCtx.destination);

const txGainNode = txCtx.createGain();
txGainNode.gain.value = $('#rig-tx-vol').val();
txGainNode.connect(txCtx.destination);

// Halt playback
function stopPlayback() {
   rxTime = rxCtx.currentTime;
}

// Halt transmit
function stopTransmit() {
   txTime = txCtx.currentTime;
}

// Flush the playback buffer then insert silence
function flushPlayback() {
   const sampleRate = rxCtx.sampleRate;
   const silence = new Float32Array(sampleRate / 10); // 100ms silence

   const audioBuffer = rxCtx.createBuffer(1, silence.length, sampleRate);
   audioBuffer.copyToChannel(silence, 0);

   const source = rxCtx.createBufferSource();
   source.buffer = audioBuffer;
   source.connect(rxCtx.destination);

   const rxNow = rxCtx.currentTime;
   if (rxTime < rxNow) {
      rxTime = rxNow;
   }

   source.start(rxTime);
   rxTime += silence.length / sampleRate;
}

// Support for playing back raw PCM audio
function playRawPCM(buffer) {
   const pcmData = new Int16Array(buffer);
   const float32Data = new Float32Array(pcmData.length);

   for (let i = 0; i < pcmData.length; i++) {
      float32Data[i] = pcmData[i] / 32768;
   }

   const samplesPerPacket = float32Data.length;
   const sampleRate = rxCtx.sampleRate;
   const duration = samplesPerPacket / sampleRate;

   const audioBuffer = rxCtx.createBuffer(1, samplesPerPacket, sampleRate);
   audioBuffer.copyToChannel(float32Data, 0);

   const source = rxCtx.createBufferSource();
   source.buffer = audioBuffer;
   source.connect(rxGainNode);

   // Schedule the audio to play in sequence
   const rxNow = rxCtx.currentTime;
   if (rxTime < rxNow) {
      rxTime = rxNow;
   }

   source.start(rxTime);
   rxTime += duration;
}

//$('#rig-rx-vol').on('input', function () {
$('#rig-rx-vol').change(function() {
   rxGainNode.gain.value = parseFloat($(this).val());
   console.log("RX vol:", rxGainNode.gain.value);
});

$('#rig-tx-vol').change(function() {
   txGainNode.gain.value = parseFloat($(this).val());
   console.log("TX vol:", txGainNode.gain.value);
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
