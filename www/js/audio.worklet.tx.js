class TxAudioProcessor extends AudioWorkletProcessor {
   constructor() {
      console.log('[audio] Added TX processor AudioWorklet');
      super();
      this.buffer = [];
      this.port.onmessage = (event) => {
         this.buffer.push(...event.data);
      };
   }

   process(inputList, outputList, parameters) {
      // Left channel of first input
      const input = inputList[0];
      if (input.length > 0) {
          const samples = input[0]; // mono

          // Accumulate samples
          this.buffer.push(...samples);

          if (this.buffer.length >= this.bufferSize) {
              const chunk = this.buffer.slice(0, this.bufferSize);
              this.buffer = this.buffer.slice(this.bufferSize);

              this.port.postMessage(chunk);
          }
      }
      return true;
   }
}
registerProcessor('tx-processor', TxAudioProcessor);
