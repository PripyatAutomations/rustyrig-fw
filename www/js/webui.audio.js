const WebUiAudio = {
   rxCtx: null,
   rxTime: 0,
   txCtx: null,
   txTime: 0,
   rxGainNode: null,
   txGainNode: null,

   stopPlayback() {
      this.rxTime = this.rxCtx.currentTime;
   },

   stopTransmit() {
      this.txTime = this.txCtx.currentTime;
   },

   flushPlayback() {
      const sampleRate = this.rxCtx.sampleRate;
      const silence = new Float32Array(sampleRate / 10); // 100ms silence

      const audioBuffer = this.rxCtx.createBuffer(1, silence.length, sampleRate);
      audioBuffer.copyToChannel(silence, 0);

      const source = this.rxCtx.createBufferSource();
      source.buffer = audioBuffer;
      source.connect(this.rxCtx.destination);

      const rxNow = this.rxCtx.currentTime;
      if (this.rxTime < rxNow) {
         this.rxTime = rxNow;
      }

      source.start(this.rxTime);
      this.rxTime += silence.length / sampleRate;
   },

   playRawPCM(buffer) {
      const pcmData = new Int16Array(buffer);
      const float32Data = new Float32Array(pcmData.length);

      for (let i = 0; i < pcmData.length; i++) {
         float32Data[i] = pcmData[i] / 32768;
      }

      const samplesPerPacket = float32Data.length;
      const sampleRate = this.rxCtx.sampleRate;
      const duration = samplesPerPacket / sampleRate;

      const audioBuffer = this.rxCtx.createBuffer(1, samplesPerPacket, sampleRate);
      audioBuffer.copyToChannel(float32Data, 0);

      const source = this.rxCtx.createBufferSource();
      source.buffer = audioBuffer;
      source.connect(this.rxGainNode);

      const rxNow = this.rxCtx.currentTime;
      if (this.rxTime < rxNow) {
         this.rxTime = rxNow;
      }

      source.start(this.rxTime);
      this.rxTime += duration;
   },

   // XXX: Here we should figure out what kind of frame it is and send to appropriate parser
   handle_binary_frame(evt) {
      this.playRawPCM(evt.data);
   },

   webui_audio_start() {
      console.log("Starting WebUiAudio");
      if (!window.socket) {
         console.log("WebUiAudio: No socket :(");
         return;
      }

//      this.rxCtx = new AudioContext({ sampleRate: 44100 });
      this.rxCtx = new AudioContext({ sampleRate: 16000 });
      this.rxTime = this.rxCtx.currentTime;

//      this.txCtx = new AudioContext({ sampleRate: 44100 });
      this.txCtx = new AudioContext({ sampleRate: 16000 });
      this.txTime = this.txCtx.currentTime;

      this.rxGainNode = this.rxCtx.createGain();
      this.rxGainNode.gain.value = $('#rig-rx-vol').val();
      this.rxGainNode.connect(this.rxCtx.destination);

      this.txGainNode = this.txCtx.createGain();
      this.txGainNode.gain.value = $('#rig-tx-vol').val();
      this.txGainNode.connect(this.txCtx.destination);

      if (this.rxCtx.state === 'suspended') {
         this.rxCtx.resume().then(() => console.log("RX context resumed"));
      }
      if (this.txCtx.state === 'suspended') {
         this.txCtx.resume().then(() => console.log("TX context resumed"));
      }
   }
};

// Volume change hooks
$('#rig-rx-vol').change(function() {
   if (WebUiAudio.rxGainNode) {
      WebUiAudio.rxGainNode.gain.value = parseFloat($(this).val());
      console.log("RX vol:", WebUiAudio.rxGainNode.gain.value);
   }
});

$('#rig-tx-vol').change(function() {
   if (WebUiAudio.txGainNode) {
      WebUiAudio.txGainNode.gain.value = parseFloat($(this).val());
      console.log("TX vol:", WebUiAudio.txGainNode.gain.value);
   }
});



if (!window.webui_inits) window.webui_inits = [];

window.webui_inits.push(function webui_audio_init() {
   $('button#use-audio').click(WebUiAudio.webui_audio_start());
});
