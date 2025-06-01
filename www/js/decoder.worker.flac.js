// decoder-worker.js
importScripts('/js/libflac.min.js'); // or wherever you keep it

let decoder;

onmessage = function(e) {
  const { type, data } = e.data;

  if (type === 'init') {
    decoder = Flac.create_libflac_decoder(); // or your wrapper
    // configure as needed
  } else if (type === 'ogg_chunk') {
    const pcm = decode_flac_chunk(data); // Float32Array[]
    for (let ch = 0; ch < pcm.length; ++ch) {
      postMessage({ type: 'pcm', samples: pcm[ch] }, [pcm[ch].buffer]);
    }
  }
};
