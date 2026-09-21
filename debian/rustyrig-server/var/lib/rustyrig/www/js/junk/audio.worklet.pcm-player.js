// audio-worklet.js
class PCMPlayerProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.buffer = [];
    this.port.onmessage = (e) => {
      const data = e.data;
      if (data.type === 'pcm') {
         this.buffer.push(new Float32Array(data.samples));
      }
    };
  }

  process(_, outputs) {
    const output = outputs[0];
    const channel = output[0];
    if (this.buffer.length > 0) {
      const frame = this.buffer.shift();
      channel.set(frame.subarray(0, channel.length));
    } else {
      channel.fill(0); // silence
    }
    return true;
  }
}

registerProcessor('pcm-player', PCMPlayerProcessor);
