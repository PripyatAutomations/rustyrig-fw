// Run our startup tasks
// XXX: Load localstorage settings or request from server (this will send defaults if no prefs stored)
//    -- Save to localStorage if not already there
// XXX: Register an interest in events on ws
// XXX: Update the display whenever websocket event comes in
var socket;
var ws_kicked = false;		// were we kicked? stop autoreconnects if so
let reconnecting = false;
let reconnectDelay = 1000;	// Start with 1 second
let reconnectInterval = [1000, 5000, 10000, 30000, 30000, 30000, 30000, 60000, 60000, 120000]; // Delay intervals in ms
var reconnectIndex = 0; 	// Index to track the current delay
var reconnectTimer;  		// so we can stop reconnects
var chat_ding;			// sound widget for chat ding
var join_ding;			// sound widget for join ding
var logged_in;			// Did we get an AUTHORIZED response?
var auth_user;
var auth_token;
var remote_nonce;
var login_user;

// This sends the first stage of the login process
function try_login() {
   login_user = $('input#user').val().toUpperCase();
   console.log("Logging in as " + login_user + "...");
   var msgObj = {
      "auth": {
         "cmd": "login",
         "user": login_user
      }
   };
   socket.send(JSON.stringify(msgObj));
}

$(document).ready(function() {
   if (!logged_in) {
      // put a chroma-hash widget on password fields
//      var chroma_hash = $("input:password").chromaHash({ bars: 3, minimum: 3, salt:"63d38fe86e1ea020d1dc945a10664d80" });
      $('#win-login input#user').focus();
   }

   // If the password field is changed
   $('input#user').change(function() {
      // Cache the username and force to upper case
      login_user = $('input#user').val().toUpperCase();
      $('input#user').val = login_user;
   });


   // When the form is submitted, we need to send the username and wait for a nonce to come back
   $('form#login').submit(function(evt) {
      let user = $("input#user");
      let pass = $("input#pass");

      if (user.val().trim() === "") {
         flashRed(user);
         event.preventDefault();
         return;
      }

      if (pass.val().trim() === "") {
         flashRed(pass);
         event.preventDefault();
         return;
      }

      // Since this is a manual reconnect attempt, unset ws_kicked which would block it
      ws_kicked = false;
      // we need to re-authenticate
      logged_in = false;
      ws_connect();
      evt.preventDefault();
   });

   $('input#reset').click(function(evt) {
      console.log("Form reset");
   });
   chat_ding = document.getElementById('chat-ding');
   form_disable(true);

   // clear button
   $('#clear-btn').click(function() {
      $('#chat-box').empty(); // Clear the chat messages
      $('#chat-box').scrollTop($('#chat-box')[0].scrollHeight); // Auto-scroll to the bottom
      $('#chat-input').focus();
   });

   // Send message to the server when "Send" button is clicked
   $('#send-btn').click(function() {
      var message = $('#chat-input').val().trim();

      // Determine if the message is a command, otherwise send it off as chat
      if (message) {
         if (message.charAt(0) == '/') {
            console.log("got CHAT command:", message);
            var args = message.split(' ');

            if (args.length > 0 && args[0].startsWith('/')) {
               args[0] = args[0].slice(1);  // Remove the first character if /
            }

            var command = args[0];
/* XXX: I dont like this but some people do?
            if (!command) {
               append_chatbox('<div><span class="error">👽 Empty command? -- ' + message + '</span></div>');
               return;
            }
*/
            switch(command.toLowerCase()) {
               case 'ban':
                  console.log("Send BAN for", args[1]);
                  sendCommand(command, args[1]);
                  break;
               case 'edit':
                  console.log("Send EDIT for", args[1]);
                  sendCommand(command, args[1]);
                  break;
               case 'kick':
                  console.log("Sending KICK for", args[1]);
                  sendCommand(command, args[1]);
                  break;
               case 'mute':
                  console.log("Send MUTE", args[1]);
                  sendCommand(command, args[1]);
                  break;
               case 'whois':
                  console.log("Send WHOIS", args[1]);
                  sendCommand(command, args[1]);
                  break;
               default:
                  append_chatbox('<div><span class="error">👽 Invalid command:' + command + '</span></div>');
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
            socket.send(JSON.stringify(msgObj));
            var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
            append_chatbox('<div>' + my_ts + ' <span class="chat-my-msg-prefix">&nbsp;==>&nbsp;</span><span class="chat-my-msg">' + message + '</span></div>');
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
         if (logged_in) {
            $('#chat-input').focus();
         } else {
            $('form#login input#user').focus();
         }
      }
         
   });

   // Ensure #chat-box does not accidentally become focusable
   $('#chat-box').attr('tabindex', '-1');

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

async function sha1Hex(str) {
   const buffer = new TextEncoder().encode(str);
   const hashBuffer = await crypto.subtle.digest("SHA-1", buffer);
   return Array.from(new Uint8Array(hashBuffer))
      .map(b => b.toString(16).padStart(2, "0"))
      .join("");
}

async function authenticate(login_user, login_pass, auth_token) {
   var hashed_pass = await sha1Hex(login_pass);  // Wait for the hash to complete
   var msgObj = {
      "auth": {
         "cmd": "pass",
         "user": login_user,
         "pass": hashed_pass,
         "token": auth_token
      }
   };
   return msgObj;
}

function cul_update(message) {
   $('#cul-list').empty();
   const users = message.talk.users;
   users.forEach(user => {
      const li = `<li>
                     <span class="chat-user-list" onclick="show_user_menu('${user}')">
                        <span class="cul-self">${user}</span>
                     </span>
                  </li>`;
      $('#cul-list').append(li);
   });
}

function show_user_menu(username) {
   // Update the user menu with the username
   $('#user-menu').html(`
      User: ${username}<br/>
      <span id="user-menu-items">
         <ul>
            <li><a href="mailto:user_email" target="_blank">Email</a></li>
            <li><button id="whois-user">Whois</button></li>
            <hr width="50%"/>
            <li><button id="mute-user">Mute</button></li>
            <li><button id="kick-user">Kick</button></li>
            <li><button id="ban-user">Lock&amp;Kick</button></li>
         </ul>
      </span>
   `);

   // Attach event listeners to buttons using jQuery
   $('#whois-user').on('click', function() {
      sendCommand('whois', username);
   });

   $('#mute-user').on('click', function() {
      sendCommand('mute', username);
   });

   $('#kick-user').on('click', function() {
      sendCommand('kick', username);
   });

   $('#ban-user').on('click', function() {
      sendCommand('ban', username);
   });
}

// Function to send commands over WebSocket
function sendCommand(cmd, target) {
   const msgObj = {
      "talk": {
         "cmd": cmd,
         "token": auth_token,  // assuming auth_token is available
         "target": target
      }
   };
   socket.send(JSON.stringify(msgObj));
   console.log(`Sent command: /${cmd} ${target}`);
}

function ws_connect() {
   // Clear any previous reconnecting flag
   reconnecting = false;
   show_connecting(true);
   socket = new WebSocket(getWebSocketUrl());

   if (ws_kicked == true) {
      // Prevent reconnecting for a moment at least
      console.log("Preventing auto-reconnect - we were kicked");
      return;
   }

   socket.onopen = function() {
      ws_kicked = false;
      show_connecting(false);
      try_login();
      form_disable(false);
      var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
      append_chatbox('<div class="chat-status">' + my_ts + ' 👽 WebSocket connected.</div>');
      reconnecting = false; 		// Reset reconnect flag on successful connection
      reconnectDelay = 1000; 		// Reset reconnect delay to 1 second
   };

   /* NOTE: On error sorts this out for us */
   socket.onclose = function() {
      if (ws_kicked != true && reconnecting == false) {
         console.log("Auto-reconnecting ws (socket closed)");
         handleReconnect();
      }
   };

   // When there's an error with the WebSocket
   socket.onerror = function(error) {
      var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
      append_chatbox('<div class="chat-status error">' + my_ts + ' 👽 WebSocket error: ', error, 'occurred.</div>');

      if (ws_kicked != true && reconnecting == false) {
         console.log("Auto-reconnecting ws (on-error)");
         handleReconnect();
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
               var msg_ts = msg_timestamp(msgObj.talk.ts);
               append_chatbox('<div>' + msg_ts + ' <span class="chat-msg-prefix">&lt;' + sender + '&gt;&nbsp;</span><span class="chat-msg">' + message + '</span></div>');

               // Play bell sound if the bell button is checked
               if ($('#bell-btn').data('checked')) {
                  chat_ding.currentTime = 0;  // Reset audio to start from the beginning
                  chat_ding.play();
               }
            } else if (cmd === 'join') {
               var user = msgObj.talk.user;
               if (user) {
                  var msg_ts = msg_timestamp(msgObj.talk.ts);
                  append_chatbox('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + user + '&nbsp;</span><span class="chat-msg">joined the chat</span>&nbsp;***</div>');
               } else {
                  console.log("got join for undefined user, ignoring");
               }
            } else if (cmd === 'kick') {
               // XXX: Display a message in chat about the user being kicked and by who/why
               console.log("Kick command received");
            } else if (cmd === 'mute') {
               // XXX: If this mute is for us, disable the send button
               // XXX: and show an alert.
               console.log("Mute command received");
            } else if (cmd === "quit") {
               var user = msgObj.talk.user;
               if (user) {
                  var msg_ts = msg_timestamp(msgObj.talk.ts);
                  append_chatbox('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + user + '&nbsp;</span><span class="chat-msg">left the chat</span>&nbsp;***</div>');
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
               console.log("Authentication error! Status: ", error);
               $('span#sub-login-error-msg').empty();
               $('span#sub-login-error-msg').append("<span>", error, "</span>");

               // Get rid of message after about 20 seconds
               setTimeout(function() { $('div#sub-login-error').hide(); }, 20000);

               $('div#sub-login-error').show();
               $('button#login-err-ok').focus();

               // Stop auto-reconnecting
               stopReconnecting();
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

                  logged_in = true;

                  hide_login_window();
                  var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
                  append_chatbox('<div><span class="msg-connected">' + my_ts + ' 👽&nbsp;***&nbsp Welcome back, ' + auth_user + '&nbsp;***</span></div>');
                  console.log("Got AUTHORIZED from server as username: ", auth_user, " with token ", auth_token);
                  break;
               case 'challenge':
                  var nonce = msgObj.auth.nonce;
                  var token = msgObj.auth.token;

                  if (token) {
                     auth_token = token;
                  }

                  var login_pass = $('input#pass').val();
                  var hashed_pass = sha1Hex(login_pass);
// XXX: This needs reworked a bit to user the double-hashing for replay protection
//                  var firstHash = sha1Hex(utf8Encode(login_pass)); // Ensure correct encoding
//                  var combinedString = firstHash + '+' + nonce;
//                  var hashed_pass = sha1Hex(utf8Encode(combinedString));
                  // here we use an async call to crypto.simple
                  authenticate(login_user, login_pass, auth_token).then(msgObj => {
                     var msgObj_t = JSON.stringify(msgObj);
                     console.log("Got challenge with nonce ", nonce, ", sending response", msgObj_t);
                     socket.send(msgObj_t);
                  });
                  break;
               case 'expired':
                  console.log("Session expired!");
                  show_login_win();
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
      }
   };
   return socket;
}

function stopReconnecting() {
   ws_kicked = true;
   if (reconnectTimer) {
      clearTimeout(reconnectTimer);
   }

   if (reconnecting) {
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
   var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
   append_chatbox('<div class="chat-status error">' + my_ts +  ' 👽 Reconnecting in ${reconnectDelay / 1000} seconds at ${new Date().toLocaleTimeString()}...</div>');

   // Reattempt connection after delay
   reconnectTimer = setTimeout(function() {
      ws_connect();

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
//   $('.chroma-hash').hide();
   $('div#win-chat').show();
   $('#chat-input').focus();
}

function show_login_window() {
   $('div#win-login').show();
//   $('.chroma-hash').show();
   $('div#win-chat').hide();
   $('input#user').focus();
}

// turn a unix time into [HH:MM.ss] stamp or space padding if invalid
function msg_timestamp(msg_ts) {
   if (typeof msg_ts !== "number") {
      msg_ts = Number(msg_ts); 			// Convert string to number if necessary
      if (isNaN(msg_ts)) return "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;";
   }

   let date = new Date(msg_ts * 1000); 		// Convert seconds to milliseconds
   let hh = String(date.getHours()).padStart(2, '0');
   let mm = String(date.getMinutes()).padStart(2, '0');
   let ss = String(date.getSeconds()).padStart(2, '0');
   return `[${hh}:${mm}.${ss}]`;
}
