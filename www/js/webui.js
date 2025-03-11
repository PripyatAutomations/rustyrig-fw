// Run our startup tasks
// XXX: Load localstorage settings or request from server (this will send defaults if no prefs stored)
//    -- Save to localStorage if not already there
// XXX: Register an interest in events on ws
// XXX: Update the display whenever websocket event comes in
var socket;
let reconnecting = false;
let reconnectDelay = 1000; // Start with 1 second
let reconnectInterval = [1000, 5000, 10000, 30000, 60000, 300000]; // Delay intervals in ms
var reconnectIndex = 0;  // Index to track the current delay
var reconnectTimer;  // To manage reconnect attempts
var bellAudio;		// sound widget
var auth_user;
var auth_token;
var remote_nonce;
var login_user;

$(document).ready(function() {
   // XXX: If not authenticated
   if (true) {
      // put a chroma-hash widget on password fields
      var chroma_hash = $("input:password").chromaHash({ bars: 3, minimum: 3, salt:"63d38fe86e1ea020d1dc945a10664d80" });

      $('#win-login input#user').focus();
   }

   // If the password field is change
   $('input#user').change(function() {
      // try to save the user from the form
      login_user = $('input#user').val();
   });


   // When the form is submitted, we need to send the username and wait for a nonce to come back
   $('form#login').submit(function(evt) {
      let user = $("input#user");
      let pass = $("input#pass");
      if (user.val().trim() === "") {
         flashRed(user);
         event.preventDefault(); // Prevent form submission
         return; // Stop checking further
      }

      if (pass.val().trim() === "") {
         flashRed(pass);
         event.preventDefault();
         return;
      }

      login_user = $('input#user').val();
      console.log("Logging in as", login_user, "...");
      // Send login request via websocket
      var msgObj = {
         "auth": {
            "cmd": "login",
            "user": login_user
         }
      };
      socket.send(JSON.stringify(msgObj));
      evt.preventDefault();
   });

   $('input#reset').click(function(evt) {
      console.log("Form reset");
   });
   bellAudio = document.getElementById('bell-sound'); // Get the audio element
   form_disable(true);
   createWebSocket();

   // clear button
   $('#clear-btn').click(function() {
      $('#chat-box').empty(); // Clear the chat messages
      $('#chat-box').scrollTop($('#chat-box')[0].scrollHeight); // Auto-scroll to the bottom
      $('#chat-input').focus();
   });

   // Send message to the server when "Send" button is clicked
   $('#send-btn').click(function() {
      var message = $('#chat-input').val().trim();
      // Determine if the message is a command, otherwise send it off to
      if (message) {
         if (message.charAt(0) == '/') {
            console.log("got command:", message);
            var args = message.split(' ');
            if (args.length > 0 && args[0].startsWith('/')) {
               args[0] = args[0].slice(1);  // Remove the first character if /
            }
            var command = args[0];
            if (!command) {
               append_chatbox('<div><span class="error">👽 Empty command? -- ' + message + '</span></div>');
               return;
            }

            switch(command) {
               case 'ban':
                  console.log("Send BAN", args[1]);
                  break;
               case 'edit':
                  console.log("Send EDIT", args[1]);
                  break;
               case 'kick':
                  console.log("Send KICK", args[1]);
                  break;
               case 'mute':
                  console.log("Send MUTE", args[1]);
                  break;
               case 'whois':
                  console.log("Send WHOIS", args[1]);
                  break;
               default:
                  append_chatbox('<div><span class="error">👽 Invalid command:' + message + '</span></div>');
                  break;
            }
         } else {
            var msgObj = {
               "talk": {
                  "cmd": "msg",
                  "token": auth_token,
                  "data": message
               }
            };
            socket.send(JSON.stringify(msgObj)); // Send the message to the server
            append_chatbox('<div><span class="chat-my-msg-prefix">&nbsp;==>&nbsp;</span><span class="chat-my-msg">' + message + '</span></div>');
         }
         $('#chat-input').val(''); // Clear the input field
         setTimeout(function () {
              $('#chat-input').focus();
         }, 10); // Delay to allow any event processing to finish
      }
   });

   // Also send the message when the "Enter" key is pressed
   $('#chat-input').keypress(function(e) {
      if (e.which == 13) {
         $('#send-btn').click();
      }
   });

   $(document).click(function (event) {
      // If the click is NOT on an input, button, or a focusable element
      if (!$(event.target).is('#chat-input, a, [tabindex]')) {
         $('#chat-input').focus();
      }
   });

   // Ensure #chat-box does not accidentally become focusable
   $('#chat-box').attr('tabindex', '-1'); // Prevent accidental focus

   // Support toggling the sound via bell button
   $('#bell-btn').click(function() {
       let isChecked = $(this).data('checked');
       $(this).data('checked', !isChecked);
       let newSrc = isChecked ? 'img/bell-alert-outline.png' : 'img/bell-alert.png';
       
       console.log("Setting image to:", newSrc);  // Debug log
       $('#bell-image').attr('src', newSrc);
   });
   $('#bell-btn').data('checked', true);  // Ensure proper initialization

   // User menu
   $('#um-close').click(function() {
      $('#user-menu').hide();
   });
   // deal with our keypresses
   $(document).keydown(function (e) {
      if (e.key === "ArrowUp" || e.key === "ArrowDown" || e.key === "PageUp" || e.key === "PageDown") {
         e.preventDefault(); // Prevent default behavior (e.g., focusing or scrolling)

         let chatBox = $("#chat-box");
         let scrollAmount = 30; // Adjust for smoother or faster scrolling
         let pageScrollAmount = chatBox.outerHeight(); // Scroll by the height of the chat box

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

function append_chatbox(msg) {
   $('#chat-box').append(msg);
   $('#chat-box').scrollTop($('#chat-box')[0].scrollHeight);
};

function form_disable(state) {
   $('#send-btn').prop('disabled', state);
   $('#clear-btn').prop('disabled', state);
   $('#chat-box').prop('disabled', state);
   $('#bell-btn').prop('disabled', state);
}

function show_connecting(state) {
   if (state == true) {
      $('#connecting').show();
   } else {
      $('#connecting').hide();
   }
}

function createWebSocket() {
   // Clear any previous reconnecting flag
   reconnecting = false;
   show_connecting(true);
   socket = new WebSocket(getWebSocketUrl());
   
   socket.onopen = function() {
      show_connecting(false);
      form_disable(false);
      append_chatbox('<div class="chat-status">👽 WebSocket connected.</div>');
      reconnecting = false; // Reset reconnect flag on successful connection
      reconnectDelay = 1000; // Reset reconnect delay to 1 second
   };
   
   // When the WebSocket connection is closed
   socket.onclose = function() {
       handleReconnect();
   };

   // When there's an error with the WebSocket
   socket.onerror = function(error) {
      append_chatbox('<div class="chat-status error">👽 WebSocket error: ', error, 'occurred.</div>');
      handleReconnect();
   };

   socket.onmessage = function(event) {
      var msgData = event.data;
      try {
         var msgObj = JSON.parse(msgData);
         console.log("Got: ", msgData);

         // Handle 'talk' commands
         if (msgObj.talk) {
            var cmd = msgObj.talk.cmd;  // Get the command
            var message = msgObj.talk.data;  // Get the message (if present)

            if (cmd === 'msg' && message) {
               var sender = msgObj.talk.from;

               // Handle message command
               append_chatbox('<div><span class="chat-msg-prefix">&lt;' + sender + '&gt;&nbsp;</span><span class="chat-msg">' + message + '</span></div>');

               // Play bell sound if the bell button is checked
               if ($('#bell-btn').data('checked')) {
                  bellAudio.currentTime = 0;  // Reset audio to start from the beginning
                  bellAudio.play();
               }
            } else if (cmd === 'kick') {
               // Handle 'kick' command
               console.log("Kick command received");
               // You can add code here to handle the kick action
            } else if (cmd === 'mute') {
               // Handle 'mute' command
               console.log("Mute command received");
               // You can add code here to handle the mute action
            } else {
               // Unknown or unsupported command
               console.log("Unknown talk command:", cmd);
            }
         } else if (msgObj.log) {
            var data = msgObj.log.data;
            // XXX: show in log window
            console.log("log message:", data);
         } else if (msgObj.auth) {
            var cmd = msgObj.auth.cmd;
            switch (cmd) {
               case 'authorized':
                  if (msgObj.auth.user) {
                     auth_user = msgObj.auth.user;
                  }

                  if (msgObj.auth.token) {
                     auth_token = msgObj.auth.token;
                  }

                  hide_login_window();

                  append_chatbox('<div><span class="msg-connected">👽&nbsp;***&nbsp Welcome back, ' + auth_user + '&nbsp;***</span></div>');
                  console.log("Got AUTHORIZED from server as username: ", auth_user, " with token ", auth_token);
                  break;
               case 'challenge':
                  var nonce = msgObj.auth.nonce;
                  var token = msgObj.auth.token;

                  if (token) {
                     auth_token = token;
                  }

                  var firstHash = sha1Hex(utf8Encode(login_user)); // Ensure correct encoding
                  var combinedString = firstHash + '+' + nonce;
                  var hashed_pass = sha1Hex(utf8Encode(combinedString));
                  var msgObj = {
                     "auth": {
                        "cmd": "pass",
                        "user": login_user,
                        "pass": hashed_pass,
                        "token": auth_token
                     }
                  };
                  var msgObj_t = JSON.stringify(msgObj);
                  console.log("Got challenge with nonce ", nonce, ", sending response", msgObj_t);
                  socket.send(msgObj_t);

                  break;
               case 'error':
                  var error = msgObj.auth.error;
                  console.log("Authentication error! Status: ", error);
                  // Stop auto-reconnecting
                  alert("Authentication error: " + error);
                  break;
               case 'expired':
                  console.log("Session expired!");
                  show_login_win();
                  break;
               default:
                  console.log("Unknown auth command: ", cmd);
                  break;
            }
         }
      } catch (e) {
         console.error("Error parsing message:", e);
      }
   };
   return socket;
}

function stopReconnecting() {
   if (reconnecting) {
      reconnectTImer.stop();
      reconnecting = false;
   }
   show_connecting(false);
   reconnectIndex = 0;
}

function handleReconnect() {
   if (reconnecting) {
      return;
   }

   reconnecting = true;
   show_connecting(true);
   append_chatbox(`<div class="chat-status error">👽 Reconnecting in ${reconnectDelay / 1000} seconds at ${new Date().toLocaleTimeString()}...</div>`);
   
   // Reattempt connection after delay
   reconnectTimer = setTimeout(function() {
      createWebSocket();

      // increase delay for the next try
      reconnectDelay = reconnectInterval[Math.min(reconnectInterval.length - 1, reconnectInterval.indexOf(reconnectDelay) + 1)];
   }, reconnectDelay);
}

function getWebSocketUrl() {
   var protocol = window.location.protocol === "https:" ? "wss://" : "ws://";
   return protocol + window.location.hostname + (window.location.port ? ":" + window.location.port : "") + "/ws/";
}

function flashRed(element) {
   element.focus()
   element.css("border", "2px solid red");
   setTimeout(() => {
      element.css("border", "");
   }, 2000);
}

function hide_login_window() {
   $('div#win-login').hide();
   $('.chroma-hash').hide();
   $('div#win-chat').show();
   $('#chat-input').focus();
}

function show_login_window() {
   $('div#win-login').show();
   $('.chroma-hash').show();
   $('div#win-chat').hide();
   $('input#user').focus();
}
