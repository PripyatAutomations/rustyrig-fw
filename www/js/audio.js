function start_tx_audio() {
   navigator.mediaDevices.getUserMedia({ audio: true })
      .then(function(stream) {
         const audioContext = new AudioContext();
         const mediaStreamSource = audioContext.createMediaStreamSource(stream);
         const processor = audioContext.createScriptProcessor(4096, 1, 1);

         // Create WebRTC audio encoder using an OPUS RTCRtpSender track (trick to access OPUS)
         const rtcPeerConnection = new RTCPeerConnection();
         const rtcSender = rtcPeerConnection.addTrack(stream.getAudioTracks()[0], stream);
         const rtcParams = {
   //         encodings: [{ maxBitrate: 64000 }],
            encodings: [{ maxBitrate: 128000, ptime: 20, dtx: false, maxFramerate: 60 }]
         };

         rtcSender.setParameters(rtcParams);

         mediaStreamSource.connect(processor);
         processor.connect(audioContext.destination);

         processor.onaudioprocess = function(event) {
            const inputBuffer = event.inputBuffer;
            const rawAudioData = inputBuffer.getChannelData(0);  // mono channel audio

            // Example: Convert Float32 to Int16 and send
            const int16Array = new Int16Array(rawAudioData.length);
            for (let i = 0; i < rawAudioData.length; i++) {
               int16Array[i] = rawAudioData[i] * 32767;  // Scale Float32 to Int16
            }

            // Send encoded audio over WebSocket
            socket.send(int16Array);
         };
      })
      .catch(function(err) {
         console.error('Error capturing audio: ', err);
      });
   /*
       const biquadFilter = audioContext.createBiquadFilter();
       biquadFilter.type = "highpass";
       biquadFilter.frequency.value = 300;  // Cut frequencies below 300 Hz

       mediaStreamSource.connect(biquadFilter);
       biquadFilter.connect(processor);
       processor.connect(audioContext.destination);
   */
}
