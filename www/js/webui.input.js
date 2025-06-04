
   // Native listener for ctrl + wheel
   document.addEventListener('wheel', function(e) {
      if (e.ctrlKey) {
         e.preventDefault();
      }
   }, { passive: false });

   $(document).on('keydown', function(e) {
      // Handle login field focus transition
/*
      if (document.activeElement.matches('form#login input#user')) {
         if ((e.key === 'Enter') || (e.key === 'Tab' && !e.shiftKey)) {
            e.preventDefault();
            document.querySelector('form#login input#pass')?.focus();
            return;
         }
      } else if (active_tab === 'chat') {
         handle_chat_completion(e);
      }
*/
      // Prevent zooming in/out
      if (e.ctrlKey && (
            e.key === '+' || e.key === '-' || 
            e.key === '=' || e.key === '0' || 
            e.code === 'NumpadAdd' || e.code === 'NumpadSubtract')) {
         e.preventDefault();
      } else if ((active_tab !== 'chat') && e.key === '/' && !e.ctrlKey && !e.metaKey) {
         e.preventDefault();
      } else if (e.key === "Escape") {
         if ($('#reason-modal').is(':visible')) {
            $('#reason-modal').hide('fast');
         } else if ($('#chat-whois').is(':visible')) {
            $('#chat-whois').hide('fast');
         } else if ($('#user-menu').is(':visible')) {
            $('#user-menu').hide('fast');
         } else {
            return;
         }
      // ENTER
      } else if (e.which == 13) {
         $('#send-btn').click();
         e.preventDefault();
      } else {
         return;
      }
      form_disable(false);
   });

   $(document).click(function (e) {
//      if (typeof window.RigAudio === 'undefined') {
//         // Since the user has interacted, we can start sound now
//         window.RigAudio = new WebUiAudio(window.socket);
//      }

      if ($(e.target).is('#reason-modal')) {			// focus reason input
         e.preventDefault();
         $('input#reason').focus();
      } else if ($(e.target).is('input#reason')) {		// focus reason input
         e.preventDefault();
         $('input#reason').focus();
      } else if ($(e.target).is('#chat-whois')) {		// hide whois dialog if clicked
         e.preventDefault();
         $(e.target).hide('fast');
         form_disable(false);
      } else if ($(e.target).is('#clear-btn')) {		// deal with the clear button in chat
         e.preventDefault();
         $('#chat-input').val('');
         form_disable(false);
      }
   });
