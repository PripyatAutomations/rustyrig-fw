// SHA-1 implementation in JavaScript
function sha1(msg) {
   var rotateLeft = function(n, s) {
      return (n << s) | (n >>> (32 - s));
   };

   var f = function(x, y, z) {
      return (x & y) | (~x & z);
   };

   var k = [0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6];

   msg += String.fromCharCode(0x80); // Append '1' bit (0x80)

   var len = msg.length * 8;
   while (msg.length % 64 !== 56) {
      msg += String.fromCharCode(0); // Pad with '0' bits (0x00)
   }

   msg += String.fromCharCode(0, 0, 0, 0); // Append length in bits (big-endian)
   msg += String.fromCharCode(len >>> 24 & 0xFF);
   msg += String.fromCharCode(len >>> 16 & 0xFF);
   msg += String.fromCharCode(len >>> 8 & 0xFF);
   msg += String.fromCharCode(len & 0xFF);

   var H0 = 0x67452301;
   var H1 = 0xEFCDAB89;
   var H2 = 0x98BADCFE;
   var H3 = 0x10325476;
   var H4 = 0xC3D2E1F0;

   var W = new Array(80);

   for (var i = 0; i < msg.length; i += 64) {
      var chunk = msg.slice(i, i + 64);
      for (var t = 0; t < 16; t++) {
         W[t] = (chunk.charCodeAt(t * 4) << 24) |
                (chunk.charCodeAt(t * 4 + 1) << 16) |
                (chunk.charCodeAt(t * 4 + 2) << 8) |
                (chunk.charCodeAt(t * 4 + 3));
      }

      for (var t = 16; t < 80; t++) {
         W[t] = rotateLeft(W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16], 1);
      }

      var a = H0;
      var b = H1;
      var c = H2;
      var d = H3;
      var e = H4;

      for (var t = 0; t < 80; t++) {
         var temp = rotateLeft(a, 5) + f(b, c, d) + e + W[t] + k[Math.floor(t / 20)];
         e = d;
         d = c;
         c = rotateLeft(b, 30);
         b = a;
         a = temp;
      }

      H0 += a;
      H1 += b;
      H2 += c;
      H3 += d;
      H4 += e;
   }

   var result = [H0, H1, H2, H3, H4];
   var hash = '';
   for (var i = 0; i < result.length; i++) {
      hash += ('00000000' + result[i].toString(16)).slice(-8); // Convert to hex and pad with leading zeros
   }

   return hash;
}

function utf8Encode(str) {
   return new TextEncoder().encode(str);
}

function sha1Hex(input) {
   var hash = sha1(input);
   return hash.match(/.{2}/g).join('');  // Ensure hex output matches C format
}
