/////////////////////////////////////////////
// Support for audio/visual notifiocations //
/////////////////////////////////////////////
var vol_changing = false;	// Are we currently changing the volume?\
var vol_timer;			// timer to expire Set Volume dialog
var chat_ding;			// sound widget for chat ding
var join_ding;			// sound widget for join ding
var leave_ding;			// sound widget for leave ding

function notification_init() {
   // Attach the sound objects (chat/join/leave)
   chat_ding = document.getElementById('chat-ding');
   join_ding = document.getElementById('join-ding');
   leave_ding = document.getElementById('leave-ding');

   // Set the current state of sounds from localStorage
   let default_sounds = localStorage.getItem('play_sounds');
   if (default_sounds === 'false') {
      $('button#bell-btn').data('checked', false);
   } else {
      $('button#bell-btn').data('checked', true);
   }
   let bell_isOn = $('button#bell-btn').data('checked');
   $('button#bell-btn').text(bell_isOn ? '🔔' : '🔕');

   // Support toggling mute via bell button
   $('#bell-btn').click(function() { toggle_mute(); });

/*
   // Deal with volume change events
   $("input#alert-vol").on("change", function() {
      let volume = $(this).val() / 100;
      $("audio#chat-ding, audio#join-ding, audio#leave-ding").prop("volume", volume);
   });

   // Attach function to pop up the volume dialog, if not already open
   $('button#bell-btn').hover(function() {
      if (!vol_changing) {
         vol_changing = true;
         $('input#alert-vol').show();
         vol_timer = setTimeout(function() {
            $('input#alert-vol').hide();
            vol_changing = false;
         }, 5000);
      }
   });
*/
}

function toggle_mute() {
   let $btn = $('#bell-btn');
   let current = $btn.data('checked') || false;
   let next = !current;

   $btn.data('checked', next);
   localStorage.setItem("play_sounds", next);
   console.log("Button", next);
   let newSrc = current ? 'img/bell-alert-outline.png' : 'img/bell-alert.png';
   $('#bell-image').attr('src', newSrc);
}

function play_notify_bell() {
   if ($('#bell-btn').data('checked')) {
      chat_ding.currentTime = 0; 
      chat_ding.play();
   }
}

function set_highlight(tab) {
   // Clear all highlights
   clear_highlight();

   // Set the new one
   $(`span#tab-${tab}`).addClass('chat-highlight');
}

function clear_highlight() {
   $('span[id^="tab-"]').removeClass('chat-highlight');
}
