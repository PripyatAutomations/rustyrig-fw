////////////////
// Chat stuff //
////////////////
const file_chunks = {}; // msg_id => {chunks: [], received: 0, total: N}

function send_chunked_file(base64Data, filename) {
   const chunkSize = 8000;
   const totalChunks = Math.ceil(base64Data.length / chunkSize);
   const msgId = crypto.randomUUID(); // Unique ID for this image

   for (let i = 0; i < totalChunks; i++) {
      const chunkData = base64Data.slice(i * chunkSize, (i + 1) * chunkSize);

      const msgObj = {
         talk: {
            cmd: "msg",
            ts: msg_timestamp(Math.floor(Date.now() / 1000)),
            token: auth_token,
            msg_type: "file_chunk",
            msg_id: msgId,
            chunk_index: i,
            total_chunks: totalChunks,
            filename: i === 0 ? filename : undefined, // Include filename only in the first chunk
            data: chunkData
         }
      };

      socket.send(JSON.stringify(msgObj));
   }
}

function handle_file_chunk(msgObj) {
   const { msg_id, chunk_index, total_chunks, data, filename } = msgObj.talk;
   const sender = msgObj.talk.from;

   if (!file_chunks[msg_id]) {
      file_chunks[msg_id] = { chunks: [], received: 0, total: total_chunks, filename: filename };
   }

   // Only update the filename once (from the first chunk)
   if (filename) {
      file_chunks[msg_id].filename = filename;
   }

   file_chunks[msg_id].chunks[chunk_index] = data;
   file_chunks[msg_id].received++;

   // If this is the last chunk, display it
   if (file_chunks[msg_id].received === total_chunks) {
      const fullData = file_chunks[msg_id].chunks.join('');
      const fileName = file_chunks[msg_id].filename;
      delete file_chunks[msg_id];

      const isSelf = sender === auth_user;
      const prefix = isSelf ? '===>' : `&lt;${sender}&gt;`;
      const msg_ts = msg_timestamp(msgObj.talk.ts);

      // Bound the image to 80% of the screen height
      const chatBoxHeight = $('#chat-box').innerHeight();
      const maxImgHeight = Math.floor(chatBoxHeight * 0.8) + 'px';

      const $img = $('<img>')
         .attr('src', fullData)
         .addClass('chat-img')
         .css({
            'max-height': maxImgHeight,
            'max-width': '80%',
            'height': 'auto',
            'width': 'auto'
         });

      const $min = $('<button>').addClass('img-min-btn').text('−');
      const $close = $('<button>').addClass('img-close-btn').text('X');

      const $controls = $('<div class="chat-img-controls">')
         .append($min)
         .append($close);

      const $wrap = $('<div class="chat-img-msg">')
         .append(msg_ts + `&nbsp;<span class="chat-msg-prefix">${prefix}</span><br/>`);

      // Wrap the image in a clickable link to open in a new tab
      const $imgLink = $('<a>')
         .attr('href', fullData)
         .attr('target', '_blank')
         .text(fileName) // Add the filename as a clickable text
         .append($img);

      const $imgWrap = $('<div class="chat-img-wrap">')
         .append($imgLink)  // Add the link-wrapped image
         .append($controls); // Add the minimize and close controls

      $wrap.append($imgWrap);

      chat_append($wrap.prop('outerHTML'));

      // Handle close button and minimize/restore button as before
      $(document).on('click', '.img-close-btn', function () {
         const $wrap = $(this).closest('.chat-img-wrap');
         $wrap.find('img.chat-img').remove();
         $wrap.find('.img-placeholder').remove(); // just in case
         $wrap.append('<em class="img-placeholder">[Image Deleted]</em>');
         $(this).siblings('.img-min-btn').remove(); // remove minimize if deleted
         $(this).remove();
      });

      $(document).on('click', '.img-min-btn', function () {
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
         }
      });
   }
}

let startY = null;

$(document).on('click', '.chat-img', function () {
   $('#modalImage').attr('src', this.src);
   $('#imageModal').removeClass('hidden');
});

$('#closeImageModal').on('click', () => {
   $('#imageModal').addClass('hidden').find('#modalImage').css('transform', '');
});

$(document).on('keydown', (e) => {
   if ($('#imageModal').is(':visible') && e.key === 'Escape') {
      $('#imageModal').addClass('hidden').find('#modalImage').css('transform', '');
   }
});

$('#imageModal').on('click', (e) => {
   if (e.target.id === 'imageModal') {
      $('#imageModal').addClass('hidden').find('#modalImage').css('transform', '');
   }
});

// Swipe down to close (basic touch gesture)
$('#imageModal').on('touchstart', function (e) {
   startY = e.originalEvent.touches[0].clientY;
}).on('touchend', function (e) {
   const endY = e.originalEvent.changedTouches[0].clientY;
   if (startY && endY - startY > 100) { // swipe down
      $('#imageModal').addClass('hidden').find('#modalImage').css('transform', '');
   }
   startY = null;
});
