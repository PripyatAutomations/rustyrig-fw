$(document).ready(function() {
   function displayEmojiKeyboard() {
      if ($('#emoji-popup').length) {
         $('#emoji-popup').toggle();
         return;
      }

      function saveRecent(emoji) {
         if (!window.localStorage) return;
         let recent = JSON.parse(localStorage.getItem('emojiRecent') || '[]');
         recent = recent.filter(e => e !== emoji);
         recent.unshift(emoji);
         if (recent.length > 20) recent.pop();
         localStorage.setItem('emojiRecent', JSON.stringify(recent));
      }

      function buildPopup(data) {
         let popup = $('<div id="emoji-popup" style="position:absolute; border:1px solid #ccc; background:#fff; padding:5px; width:320px; height:400px; overflow:auto; z-index:1000;">');
         let closeBtn = $('<div style="text-align:right;"><button id="close-emoji">✖</button></div>');
         let searchBox = $('<input type="text" id="emoji-search" placeholder="Search..." style="width:100%; margin:5px 0; padding:3px;">');
         let listContainer = $('<div id="emoji-list"></div>');

         popup.append(closeBtn, searchBox, listContainer);
         $('body').append(popup);

         $('#emoji-popup').css({ 
            top: $('#chat-input').offset().top + $('#chat-input').outerHeight() + 5, 
            left: $('#chat-input').offset().left 
         });

         function renderList(filter) {
            listContainer.empty();

            // Recent
            if (!filter && window.localStorage) {
               let recent = JSON.parse(localStorage.getItem('emojiRecent') || '[]');
               if (recent.length) {
                  let recentDiv = $('<div class="emoji-category" style="margin:5px 0;"><strong style="cursor:pointer;">Recently Used ▼</strong><div class="emoji-items"></div></div>');
                  recent.forEach(function(emoji){
                     recentDiv.find('.emoji-items').append('<span class="emoji-item" style="cursor:pointer; font-size:20px;">'+emoji+'</span>');
                  });
                  listContainer.append(recentDiv);
               }
            }

            // Regular emojis
            $.each(data, function(category, emojis){
               if (!Array.isArray(emojis)) return; // Skip if emojis is not an array

               let matches = emojis.filter(e => !filter || e.includes(filter));
               if (matches.length) {
                  let catDiv = $('<div class="emoji-category" style="margin:5px 0;"><strong style="cursor:pointer;">'+category+' ▼</strong><div class="emoji-items"></div></div>');
                  matches.forEach(function(emoji){
                     catDiv.find('.emoji-items').append('<span class="emoji-item" style="cursor:pointer; font-size:20px;">'+emoji+'</span>');
                  });
                  listContainer.append(catDiv);
               }
            });
         }

         renderList('');

         // Click emoji to insert into chat input
         listContainer.on('click', '.emoji-item', function(){
            let emoji = $(this).text();
            $('#chat-input').val($('#chat-input').val() + emoji);
            saveRecent(emoji);
         });

         // Close popup
         $('#close-emoji').on('click', function(){
            $('#emoji-popup').hide();
         });

         // Live search filter
         $('#emoji-search').on('input', function(){
            renderList($(this).val().trim());
         });

         // Collapsible sections
         listContainer.on('click', 'strong', function(){
            $(this).next('.emoji-items').toggle();
            let txt = $(this).text();
            $(this).text(txt.includes('▲') ? txt.replace('▲', '▼') : txt.replace('▼', '▲'));
         });

         // Keyboard navigation
         $(document).on('keydown.emojiNav', function(e){
            if (!$('#emoji-popup').is(':visible')) return;
            let focused = listContainer.find('.emoji-item.focused');
            let allItems = listContainer.find('.emoji-item:visible');

            if (!focused.length) {
               allItems.first().addClass('focused');
               return;
            }

            let index = allItems.index(focused);

            if (e.key === 'ArrowRight') {
               allItems.removeClass('focused');
               allItems.eq((index+1) % allItems.length).addClass('focused');
               e.preventDefault();
            }
            if (e.key === 'ArrowLeft') {
               allItems.removeClass('focused');
               allItems.eq((index-1+allItems.length) % allItems.length).addClass('focused');
               e.preventDefault();
            }
            if (e.key === 'ArrowDown') {
               allItems.removeClass('focused');
               allItems.eq(Math.min(index+5, allItems.length-1)).addClass('focused');
               e.preventDefault();
            }
            if (e.key === 'ArrowUp') {
               allItems.removeClass('focused');
               allItems.eq(Math.max(index-5,0)).addClass('focused');
               e.preventDefault();
            }
            if (e.key === 'Enter') {
               focused.click();
               e.preventDefault();
            }
            if (e.key === 'Escape') {
               $('#emoji-popup').hide();
               e.preventDefault();
            }
         });
      }

      let emojiData = null;
      if (window.localStorage && localStorage.getItem('emojiData')) {
         emojiData = JSON.parse(localStorage.getItem('emojiData'));
         buildPopup(emojiData);
      } else {
         $.getJSON('emojis.min.json', function(data){
            emojiData = data;
            if (window.localStorage) {
               localStorage.setItem('emojiData', JSON.stringify(data));
            }
            buildPopup(emojiData);
         });
      }
   }

   $('#emoji-btn').on('click', function(){
      displayEmojiKeyboard();
   });
});
