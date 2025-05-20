// Slurp audio from the mic input and vomit it back to the main code in www/js/webui.audio.js
class MicrophoneProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        this.buffer = [];
        this.bufferSize = 2048;
    }

    process(inputs) {
        const input = inputs[0];
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

registerProcessor('microphone-processor', MicrophoneProcessor);
