class RxAudioProcessor extends AudioWorkletProcessor {
   constructor() {
      console.log('[audio] Added RX processor AudioWorklet');
      super();
      this.buffer = [];
      this.port.onmessage = (event) => {
         this.buffer.push(...event.data);
      };
   }

   process(inputList, outputList, parameters) {
      // Left channel of first input
      const output = outputList[0];
      const channel = output[0];

      for (let i = 0; i < channel.length; i++) {
         if (this.buffer.length > 0) {
            channel[i] = this.buffer.shift();
         } else {
            channel[i] = 0.0;	// silence on underrun;
            console.log('[audio] RX underrun!');
         }
      }

      return true;
   }
}
registerProcessor('rx-processor', RxAudioProcessor);
