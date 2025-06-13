if (!window.webui_inits) window.webui_inits = [];

var audio_codec = "mu16";		// This is our default but we plan to support others via json message to switch soon
var audio_rate = 16000;			// default for mulaw

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

function decodeMulawToFloat32(buffer) {
   const mulaw = new Uint8Array(buffer);
   const float32 = new Float32Array(mulaw.length);

   for (let i = 0; i < mulaw.length; i++) {
      float32[i] = mulawDecode8(mulaw[i]) / 32768; // Normalize to [-1, 1]
   }

   return float32;
}

function mulawDecode8(u_val) {
   const MULAW_MAX = 0x1FFF;
   const BIAS = 33;

   u_val = ~u_val;

   let t = ((u_val & 0x0F) << 3) + BIAS;
   t <<= ((u_val & 0x70) >> 4);

   return (u_val & 0x80) ? -t : t;
}

function playAudioPacket(buffer, codec = 'mu16') {
   let float32Data;
   let sampleRate;

   switch (codec) {
      case 'mu08':
         float32Data = decodeMulawToFloat32(buffer);
         sampleRate = 8000;
         break;
      case 'mu16':
         float32Data = decodeMulawToFloat32(buffer);
         sampleRate = 16000;
         break;
      case 'pc16':
         float32Data = decodePCM16ToFloat32(buffer);
         sampleRate = 16000;
         break;
      case 'pc44':
         float32Data = decodePCM16ToFloat32(buffer);
         sampleRate = 44100;
         break;
      default:
         console.warn("Unknown codec:", codec);
         return;
   }

   playFloat32Samples(float32Data, sampleRate);
}

function decodePCM16ToFloat32(buffer) {
   const pcmData = new Int16Array(buffer);
   const float32Data = new Float32Array(pcmData.length);

   for (let i = 0; i < pcmData.length; i++) {
      float32Data[i] = pcmData[i] / 32768;
   }

   return float32Data;
}

function playFloat32Samples(float32Data, sampleRate) {
   const samplesPerPacket = float32Data.length;
   const duration = samplesPerPacket / sampleRate;

   const audioBuffer = rxCtx.createBuffer(1, samplesPerPacket, sampleRate);
   audioBuffer.copyToChannel(float32Data, 0);

   const source = rxCtx.createBufferSource();
   source.buffer = audioBuffer;
   source.connect(rxGainNode);

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
