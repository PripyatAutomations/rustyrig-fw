/////////////////////////////
// File Xfer / Image paste //
/////////////////////////////
const file_chunks = {}; // msg_id => {chunks: [], received: 0, total: N}

// Convert base64 data URI to Blob
function data_uri_to_blob(dataURI) {
   const byteString = atob(dataURI.split(',')[1]);
   const mimeString = dataURI.split(',')[0].split(':')[1].split(';')[0];

   const ab = new ArrayBuffer(byteString.length);
   const ia = new Uint8Array(ab);
   for (let i = 0; i < byteString.length; i++) {
      ia[i] = byteString.charCodeAt(i);
   }

   return new Blob([ab], { type: mimeString });
}

function send_chunked_file(base64Data, filename, filetype) {
   const chunkSize = 8000;
   const totalChunks = Math.ceil(base64Data.length / chunkSize);
   const msgId = crypto.randomUUID();

   for (let i = 0; i < totalChunks; i++) {
      const chunkData = base64Data.slice(i * chunkSize, (i + 1) * chunkSize);

      const msgObj = {
         talk: {
            cmd: "msg",
            ts: Math.floor(Date.now() / 1000),
            token: auth_token,
            msg_type: "file_chunk",
            msg_id: msgId,
            chunk_index: i,
            total_chunks: i === 0 ? totalChunks : undefined,
            filename: i === 0 ? filename : undefined,
            filetype: i === 0 ? filetype : undefined,
            data: chunkData
         }
      };

      socket.send(JSON.stringify(msgObj));
   }
}

function handle_file_chunk(msgObj) {
   const { msg_id, chunk_index, total_chunks, data, filename, filetype } = msgObj.talk;
   const sender = msgObj.talk.from;

   if (!file_chunks[msg_id]) {
      file_chunks[msg_id] = { chunks: [], received: 0, total: total_chunks, filename: filename, filetype: filetype };
   }

   // Only update the filename & total_chunks once (from the first chunk)
   if (filename) {
      file_chunks[msg_id].filename = filename;
   }

   if (total_chunks) {
      file_chunks[msg_id].total_chunks = total_chunks;
   }

   if (filetype) {
      file_chunks[msg_id].filename = filetype;
   }
 
   file_chunks[msg_id].chunks[chunk_index] = data;
   file_chunks[msg_id].received++;

   // If this is the last chunk, display it
   if (file_chunks[msg_id].received === file_chunks[msg_id].total_chunks) {
      const fullData = file_chunks[msg_id].chunks.join('');
      const fileName = file_chunks[msg_id].filename;
      const fileType = file_chunks[msg_id].filetype;
      delete file_chunks[msg_id];

      const isSelf = sender === auth_user;
      const prefix = isSelf ? '===>' : `&lt;${sender}&gt;`;
      const msg_ts = msg_timestamp(msgObj.talk.ts);

      const chatBoxHeight = $('#chat-box').innerHeight();
      // Bound the image to 80% of the screen height
      const maxImgHeight = Math.floor(chatBoxHeight * 0.8) + 'px';

      const blob = data_uri_to_blob(fullData);
      const blobUrl = URL.createObjectURL(blob);
      console.log("Blob ", blobUrl, " has filetype", fileType);

      const isImage = fileType && fileType.startsWith('image/');
      if (isImage) {
         const $img = $('<img>')
            .attr('src', fullData)
//          .attr('src', blobUrl)			// this is more efficient, but seems to break scrolling
            .addClass('chat-img')
            .css({
               'max-height': maxImgHeight,
               'max-width': '80%',
               'height': 'auto',
               'width': 'auto'
            });

         const $imgLink = $('<a>')
            .attr('href', blobUrl)
            .attr('target', '_blank')
            .append($img);
         const $min = $('<button>').addClass('img-min-btn').text('−');
         const $close = $('<button>').addClass('img-close-btn').text('X');

         const $controls = $('<div class="chat-img-controls">')
            .append($min)
            .append($close);

         const $wrap = $('<div class="chat-img-msg">')
            .append(msg_ts + `&nbsp;<span class="chat-msg-prefix">${prefix}</span><br/>`);

         const $imgWrap = $('<div class="chat-img-wrap">')
            .append($imgLink)  // Add the link-wrapped image
            .append($controls); // Add the minimize and close controls

         $wrap.append($imgWrap);

         chat_append($wrap.prop('outerHTML'));

         // Scroll to the newly appended image
         setTimeout(() => {
            const lastImg = $('.chat-img-msg').last()[0];
            if (lastImg) lastImg.scrollIntoView({ behavior: 'smooth', block: 'end' });
         }, 10);
      } else {
         const $fileLink = $('<a>')
            .attr('href', blobUrl)
            .attr('target', '_blank')
            .text(`Download ${fileName} (${fileType || 'unknown'})`);

         chat_append('<div class="chat-msg">$imgWrap.append($fileLink)</div>');
      }

      $('#chat-box').on('click', '.img-close-btn', function () {
          const $wrap = $(this).closest('.chat-img-wrap');
          $wrap.find('img.chat-img').remove();
          $wrap.find('.img-placeholder').remove();
          $wrap.append('<em class="img-placeholder">[Image Deleted]</em>');
          $(this).siblings('.img-min-btn').remove();
          $(this).remove();
      });

      $('#chat-box').on('click', '.img-min-btn', function () {
          const $btn = $(this);
          const $wrap = $btn.closest('.chat-img-wrap');
          const $img = $wrap.find('img.chat-img');
          let $placeholder = $wrap.find('.img-placeholder');

          if ($img.is(':visible')) {
              $img.hide();
              if (!$placeholder.length) {
                  $placeholder = $('<div class="img-placeholder">[Image Minimized]</div>');
                  $wrap.append($placeholder);
              }
              $btn.text('+');
          } else {
              $img.show();
              $placeholder.remove();
              $btn.text('−');
              setTimeout(function () {
                  $img[0].scrollIntoView({ behavior: 'smooth', block: 'end' });
              }, 10);
          }
      });
   }
}

function paste_image(e, item) {
   const file = item.getAsFile();
   const reader = new FileReader();
   reader.onload = function(evt) {
      form_disable(true);
      const dataUrl = evt.target.result;
      const confirmBox = $('<div class="img-confirm-box">')
         .append(`<p>Send this image?</p><img id="img-confirm-img" src="${dataUrl}"><br>`)
         .append('<button id="img-post" class="green-btn">Yes</button><button id="img-cancel" class="red-btn">No</button>')
         .append('<button id="img-clearclip" class="red-btn">Clr Clipbrd</button>');
         
      $('body').append(confirmBox);
      confirmBox.find('#img-post').click(() => {
         send_chunked_file(dataUrl, file.name, file.type);
         confirmBox.remove();
      });
      confirmBox.find('#img-cancel').click(() => {
         confirmBox.remove();
      });
      confirmBox.find('#img-clearclip').click(() => {
         navigator.clipboard.writeText(" ");
         confirmBox.remove();
      });
      form_disable(false);
      $('button#img-post').focus();
   };
   reader.readAsDataURL(file);
   e.preventDefault();
}

function estimate_xfer_cache_size(obj) {
   const seen = new WeakSet();
   let bytes = 0;

   function sizeOf(value) {
      if (value === null || value === undefined) return 0;
      if (typeof value === 'string') return value.length * 2;
      if (typeof value === 'number') return 8;
      if (typeof value === 'boolean') return 4;
      if (typeof value === 'object') {
         if (seen.has(value)) return 0;
         seen.add(value);
         for (const key in value) {
            bytes += sizeOf(key);
            try {
               bytes += sizeOf(value[key]);
            } catch (_) {}
         }
      }
      return bytes;
   }

   return sizeOf(obj);
}

function clear_xfer_chunks() {
   const before = estimate_xfer_cache_size(file_chunks);

   for (const key in file_chunks) {
      delete file_chunks[key];
   }

   const after = estimate_xfer_cache_size(file_chunks);
   const freedKB = ((before - after) / 1024).toFixed(1);
   console.log(`Cleared file_chunks: ~${freedKB} KB freed`);
}

$(document).ready(function() {
   // Trigger file input when button is clicked
   $('#upload-btn').on('click', function() {
      $('#file-upload').click();
   });

   // Handle file selection
   $('#file-upload').on('change', function(e) {
      const file = e.target.files[0];
      if (!file) return;

      const reader = new FileReader();
      reader.onload = function(evt) {
         const dataUrl = evt.target.result;
         form_disable(true);
         const confirmBox = $('<div class="img-confirm-box">')
            .append(`<p>Send this image?</p><img id="img-confirm-img" src="${dataUrl}" />`)
            .append('<button id="img-post" class="green-btn">Yes</button>')
            .append('<button id="img-cancel" class="gray-btn">Cancel</button>');

         $('body').append(confirmBox);

         confirmBox.find('#img-post').click(() => {
            send_chunked_file(dataUrl, file.name, file.type);
            confirmBox.remove();
            $('#file-upload').val('');
         });

         confirmBox.find('#img-cancel').click(() => {
            confirmBox.remove();
            $('#file-upload').val('');
         });

         form_disable(false);
         confirmBox.find('#img-post').focus();
      };
      reader.readAsDataURL(file);
   });
});
