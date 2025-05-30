class RxProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        this.buffer = [];
        this.port.onmessage = (event) => {
            this.buffer.push(...event.data);
        };
    }

    process(inputs, outputs) {
        const output = outputs[0];
        const channel = output[0];

        for (let i = 0; i < channel.length; i++) {
            if (this.buffer.length > 0) {
                channel[i] = this.buffer.shift();
            } else {
                channel[i] = 0.0; // silence if buffer underruns
            }
        }

        return true;
    }
}

registerProcessor('rx-processor', RxProcessor);
