// Handle unpacking and packing audio frames

function parseAudioFrame(buffer) {
    const view = new DataView(buffer);
    const chan_id = view.getUint16(0, false); // big-endian
    const seq     = view.getUint16(2, false); // big-endian
    const payload = buffer.slice(4);
    return { chan_id, seq, payload };
}

function makeAudioFrame(chan_id, seq, payload) {
    const buf = new ArrayBuffer(4 + payload.byteLength);
    const view = new DataView(buf);
    view.setUint16(0, chan_id, false); // big-endian
    view.setUint16(2, seq, false);
    new Uint8Array(buf, 4).set(new Uint8Array(payload));
    return buf;
}
