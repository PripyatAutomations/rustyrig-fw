// Here's my ugly implementation of the WebUI.
//

//////////////////////////
// Socket related stuff //
//////////////////////////
var socket;
var ws_kicked = false;		// were we kicked? stop autoreconnects if so
let reconnecting = false;
let reconnect_delay = 1;
let reconnect_interval = [1, 2, 5, 10, 30 ];
var reconnect_index = 0; 	// Index to track the current delay
var reconnect_timer;  		// so we can stop reconnects later

$(document).ready(function() {
   /////////////////////////////////////
   // Load settings from LocalStorage //
   /////////////////////////////////////
   // Do we have a preference for dark/light mode in localStorage?
   if (localStorage.getItem("dark_mode") !== "false") {
      $("#dark-theme").attr("href", "css/dark.css").removeAttr("disabled");
      $("#tab-dark").text("light");
   } else {
      $("#dark-theme").attr("href", "").attr("disabled", "disabled");
      $("#tab-dark").text("dark");
   }

   if (!logged_in) {
      login_init();
   }

   notification_init();
   chat_init();
   chat_append('<div><span class="error">***** New commands are available! See /help for chat help and !help for rig commands *****</span></div>');

   // Reset buttons
   $('input#reset').click(function(evt) {
      console.log("Form reset");
   });

   // clear buttons
   $('#clear-btn').click(function() {
      $('#chat-input').val('');
      $('#chat-input').focus();
   });

   form_disable(true);

   // Toggle display of the emoji keyboard
   $('#emoji-btn').click(function() {
       const emojiKeyboard = $('#emoji-keyboard');
       if (emojiKeyboard.is(':visible')) {
           emojiKeyboard.hide(); // Hide if already visible
       } else {
           emojiKeyboard.show(); // Show if hidden
       }
   });

   // For clicks inside the document, do stuff
   $(document).click(function (event) {
      // If the click is NOT on an input, button, or a focusable element
      if (!$(event.target).is('#chat-input, a, [tabindex]')) {
         // are we logged in?
         if (logged_in) {	// Focus the chat input field
            $('#chat-input').focus();
         } else {		// Nope, focus the user field
            $('form#login input#user').focus();
         }
      }
   });

   $(document).keydown(function (e) {
      if (e.key === "ArrowUp" || e.key === "ArrowDown" || e.key === "PageUp" || e.key === "PageDown") {
         e.preventDefault();

         let chatBox = $("#chat-box");
         let scrollAmount = 30;
         let pageScrollAmount = chatBox.outerHeight();

         if (e.key === "ArrowUp") {
            chatBox.scrollTop(chatBox.scrollTop() - scrollAmount);
         } else if (e.key === "ArrowDown") {
            chatBox.scrollTop(chatBox.scrollTop() + scrollAmount);
         } else if (e.key === "PageUp") {
            chatBox.scrollTop(chatBox.scrollTop() - pageScrollAmount);
         } else if (e.key === "PageDown") {
            chatBox.scrollTop(chatBox.scrollTop() + pageScrollAmount);
         }
      }
   });
});

function make_ws_url() {
   var protocol = window.location.protocol === "https:" ? "wss://" : "ws://";
   return protocol + window.location.hostname + (window.location.port ? ":" + window.location.port : "") + "/ws/";
}

function ws_connect() {
   var rc = reconnecting;
   reconnecting = false;
   show_connecting(true);
   socket = new WebSocket(make_ws_url());

   // Was the websockets connection kicked? If so, don't reconnect
   if (ws_kicked == true) {
      console.log("Preventing auto-reconnect - we were kicked");
      return;
   }

   socket.onopen = function() {
      ws_kicked = false;
      in_connect = false;
      show_connecting(false);
      try_login();
      form_disable(false);
      var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
      chat_append('<div class="chat-status">' + my_ts + '&nbsp;WebSocket connected.</div>');
      reconnecting = false; 		// Reset reconnect flag on successful connection
      reconnect_delay = 1; 		// Reset reconnect delay to 1 second
   };

   /* NOTE: On error sorts this out for us */
   socket.onclose = function() {
      if (ws_kicked != true && reconnecting == false) {
         console.log("Auto-reconnecting ws (socket closed)");
         handle_reconnect();
      }
   };

   // When there's an error with the WebSocket
   socket.onerror = function(error) {
      var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
      if (rc === false) {
         chat_append('<div class="chat-status error">' + my_ts + '&nbsp;WebSocket error: ', error, 'occurred.</div>');
      }

      if (ws_kicked != true && reconnecting == false) {
         console.log("Auto-reconnecting ws (on-error)");
         handle_reconnect();
      }
   };

   socket.onmessage = function(event) {
      var msgData = event.data;
      try {
         var msgObj = JSON.parse(msgData);

         if (msgObj.talk) {
            var cmd = msgObj.talk.cmd;
            var message = msgObj.talk.data;

            if (cmd === 'msg' && message) {
               var sender = msgObj.talk.from;
               var msg_type = msgObj.talk.msg_type;
               var msg_ts = msg_timestamp(msgObj.talk.ts);

               if (msg_type === "file_chunk") {
                  handle_file_chunk(msgObj);
               } else if (msg_type === "action" || msg_type == "pub") {
                  message = msg_create_links(message);

                  // Don't play a bell or set highlight on SelfMsgs
                  if (sender === auth_user) {
                     if (msg_type === 'action') {
                        chat_append('<div>' + msg_ts + ' <span class="chat-my-msg-prefix">&nbsp;==>&nbsp;</span>***&nbsp;' + sender + '&nbsp;***&nbsp;<span class="chat-my-msg">' + message + '</span></div>');
                     } else {
                        chat_append('<div>' + msg_ts + ' <span class="chat-my-msg-prefix">&nbsp;==>&nbsp;</span><span class="chat-my-msg">' + message + '</span></div>');
                     }
                  } else {
                     if (msg_type === 'action') {
                        chat_append('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">&nbsp;' + sender + '&nbsp;</span>***&nbsp;<span class="chat-msg">' + message + '</span></div>');
                     } else {
                        chat_append('<div>' + msg_ts + ' <span class="chat-msg-prefix">&lt;' + sender + '&gt;&nbsp;</span><span class="chat-msg">' + message + '</span></div>');
                     }

                     play_notify_bell();
                     set_highlight("chat");

                     // XXX: Update the window title to show a pending message
                  }
               }
            } else if (cmd === 'join') {
               var user = msgObj.talk.user;

               if (user) {
                  var msg_ts = msg_timestamp(msgObj.talk.ts);
                  chat_append('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + user + '&nbsp;</span><span class="chat-msg">joined the chat</span>&nbsp;***</div>');
                  // Play join (door open) sound if the bell button is checked
                  if ($('#bell-btn').data('checked')) {
                     if (!(user === auth_user)) {
                        join_ding.currentTime = 0;  // Reset audio to start from the beginning
                        join_ding.play();
                     }
                  }
               } else {
                  console.log("got join for undefined user, ignoring");
               }
            } else if (cmd === 'kick') {
               // XXX: Display a message in chat about the user being kicked and by who/why
               console.log("Kick command received");
               // Play leave (door close) sound if the bell button is checked
               if ($('#bell-btn').data('checked')) {
                  if (!(user === auth_user)) {
                     leave_ding.currentTime = 0;  // Reset audio to start from the beginning
                     leave_ding.play();
                  }
               }
            } else if (cmd === 'mute') {
               // XXX: If this mute is for us, disable the send button
               // XXX: and show an alert.
               console.log("Mute command received");
            } else if (cmd === "quit") {
               var user = msgObj.talk.user;
               if (user) {
                  var msg_ts = msg_timestamp(msgObj.talk.ts);
                  chat_append('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + user + '&nbsp;</span><span class="chat-msg">left the chat</span>&nbsp;***</div>');
                  // Play leave (door close) sound if the bell button is checked
                  if ($('#bell-btn').data('checked')) {
                     if (!(user === auth_user)) {
                        leave_ding.currentTime = 0;  // Reset audio to start from the beginning
                        leave_ding.play();
                     }
                  }
               } else {
                  console.log("got %s for undefined user, ignoring", cmd);
               }
            } else if (cmd === "names") {
               cul_update(msgObj);
            } else if (cmd === "unmute") {
               // XXX: If unmute is for us, enable send button and let user know they can talk again
            } else {
               console.log("Unknown talk command:", cmd, "msg: ", msgData);
            }
         } else if (msgObj.log) {
            var data = msgObj.log.data;
            // XXX: show in log window
            console.log("log message:", data);
         } else if (msgObj.auth) {
            var cmd = msgObj.auth.cmd;
            var error = msgObj.auth.error;

            if (error) {
               var error = msgObj.auth.error;
               stop_reconnecting();

               var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
               chat_append('<div><span class="error">' + my_ts + '&nbsp;Error: ' + error + '!</span></div>');
               show_login_window();
               $('span#sub-login-error-msg').empty();
               $('span#sub-login-error-msg').append("<span>", error, "</span>");

               // Get rid of message after about 20 seconds
               setTimeout(function() { $('div#sub-login-error').hide(); }, 20000);

               $('div#sub-login-error').show();
               $('button#login-err-ok').click(function() {
                  $('div#sub-login-error').hide();
                  $('span#sub-login-error-msg').empty();
                  $('input#user').focus();
               });
               $('button#login-err-ok').focus();
               return false;
            }

            switch (cmd) {
               case 'authorized':
                  // Save the auth_user as it's the only reputable source of truth for user's name
                  if (msgObj.auth.user) {
                     auth_user = msgObj.auth.user;
                  }

                  if (msgObj.auth.token) {
                     auth_token = msgObj.auth.token;
                  }

                  if (msgObj.auth.privs) {
                     auth_privs = msgObj.auth.privs;
                  }

                  logged_in = true;
                  show_chat_window();
                  var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
                  chat_append('<div><span class="msg-connected">' + my_ts + '&nbsp;***&nbspWelcome back, ' + auth_user + ', You have ' + auth_privs + ' privileges</span></div>');
                  break;
               case 'challenge':
                  var nonce = msgObj.auth.nonce;
                  var token = msgObj.auth.token;

                  if (token) {
                     auth_token = token;
                  }

                  var login_pass = $('input#pass').val();
                  var hashed_pass = sha1_hex(login_pass);

                  // here we use an async call to crypto.simple
                  authenticate(login_user, login_pass, auth_token, nonce).then(msgObj => {
                     var msgObj_t = JSON.stringify(msgObj);
                     socket.send(msgObj_t);
                  });
                  break;
               case 'expired':
                  console.log("Session expired!");
                  chat_append('<div><span class="error">Session expired, logging out</span></div>');
                  show_login_window();
                  break;
               default:
                  console.log("Unknown auth command: ", cmd);
                  break;
            }
         } else {
            console.log("Got unknown message from server: ", msgData);
         }
      } catch (e) {
         console.error("Error parsing message:", e);
         console.log("Unknown data: ", msgData);
      }
   };
   return socket;
}

function stop_reconnecting() {
   ws_kicked = true;
   if (reconnect_timer) {
      clearTimeout(reconnect_timer);
   }

   if (reconnecting) {
      reconnecting = false;
   }
   show_connecting(false);
   reconnect_index = 0;
}

function handle_reconnect() {
   if (reconnecting) {
      return;
   }

   reconnecting = true;
   show_connecting(true);
   var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
   chat_append('<div class="chat-status error">' + my_ts + '&nbsp; Reconnecting in ' + reconnect_delay + ' sec</div>');

   // Delay reconnecting for a bit
   reconnect_timer = setTimeout(function() {
      ws_connect();

      // increase delay for the next try
      reconnect_delay = reconnect_interval[Math.min(reconnect_interval.length - 1, reconnect_interval.indexOf(reconnect_delay) + 1)];
   }, reconnect_delay * 1000);
}

function flash_red(element) {
   // XXX: We should save the old border, if any, then restore it below
   element.focus()
   var old_border = element.css("border");
   element.css("border", "2px solid red");
   setTimeout(() => {
      let restore_border = old_border;
      element.css("border", restore_border);
   }, 2000);
}

function toggle_dark_mode() {
   const $darkTheme = $("#dark-theme");
   const $toggleBtn = $("#tab-dark");
   const darkCSS = "css/dark.css";

   let dark_mode = localStorage.getItem("dark_mode") !== "false"; 
   dark_mode = !dark_mode;

   if (dark_mode) {
      console.log("Set Dark Mode");
      $darkTheme.attr("href", darkCSS).removeAttr("disabled");
      $toggleBtn.text("light");
   } else {
      console.log("Set Light Mode");
      $darkTheme.attr("href", "").attr("disabled", "disabled");
      $toggleBtn.text("dark");
   }

   localStorage.setItem("dark_mode", dark_mode);
}

function form_disable(state) {
   $('#emoji-btn').prop('disabled', state);
   $('#send-btn').prop('disabled', state);
   $('#clear-btn').prop('disabled', state);
   $('#chat-box').prop('disabled', state);
   $('#bell-btn').prop('disabled', state);
}

function show_login_window() {
   $('button#submit').prop('disabled', false);
   $('div#win-chat').hide();
   $('div#win-config').hide();
   $('div#win-syslog').hide();
   $('div#win-login').show();
   $('input#user').focus();
   $('div#tabstrip').hide();
//   $('.chroma-hash').show();
}

function show_chat_window() {
//   $('.chroma-hash').hide();
   $('div#win-login').hide();
   $('div#win-rig').hide();
   $('div#win-config').hide();
   $('div#win-syslog').hide();
   $('div#tabstrip').show();
   $('div#win-chat').show();
   $('#chat-input').focus();
}

function show_rig_window() {
//   $('.chroma-hash').hide();
   $('div#win-login').hide();
   $('div#win-chat').hide();
   $('div#win-config').hide();
   $('div#win-syslog').hide();
   $('div#tabstrip').show();
   $('div#win-rig').show();
}

function show_config_window() {
//   $('.chroma-hash').hide();
   $('div#win-rig').hide();
   $('div#win-chat').hide();
   $('div#win-login').hide();
   $('div#win-syslog').hide();
   $('div#tabstrip').show();
   $('div#win-config').show();
}

function show_syslog_window() {
//   $('.chroma-hash').hide();
   $('div#win-rig').hide();
   $('div#win-chat').hide();
   $('div#win-login').hide();
   $('div#win-config').hide();
   $('div#tabstrip').show();
   $('div#win-syslog').show();
}
