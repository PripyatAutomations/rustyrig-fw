////////////////////////
// Latency calculator //
////////////////////////
let latency_samples = [];
var latency_timer;
const latency_max_samples = 50;

function latency_send_pings(socket) {
   const now = Date.now();
   socket.send(JSON.stringify({type: 'ping', ts: now}));
}

function latency_check_init() {
   socket.onmessage = function(event) {
      let msg = JSON.parse(event.data);

      if (msg.type === 'pong' && msg.ts) {
         let rtt = Date.now() - msg.ts;
         latency_samples.push(rtt);

         if (latency_samples.length > latency_max_samples) {
            latency_samples.shift();
         }
      }
   };
}

function latency_toggle_check() {
   if (!latency_timer) {
      latency_timer = setInterval(latency_send_pings, 2000);
   } else {
      // Destroy the timer instance
      
   }
}

function latency_get_avg() {
   if (latency_samples.length === 0) {
      return null;
   }

   let sum = latency_samples.reduce((a, b) => a + b, 0);
   return sum / latency_samples.length;
}
