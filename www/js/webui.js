//
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
var reconnect_timer;  		// so we can stop reconnects

////////////////////////////
// Authentication/Session //
////////////////////////////
var logged_in;			// Did we get an AUTHORIZED response?
var auth_user;			// Username the server sent us
var auth_token;			// Session token the server gave us during LOGIN
var auth_privs;			// Privileges the server has granted us
var remote_nonce;		// The login nonce, which is used to derive a replay protected password response
var login_user;			// Username we send to the server

///////////////////
// Volume slider //
///////////////////
var vol_changing = false;	// Are we currently changing the volume?\
var vol_timer;			// timer to expire Set Volume dialog
var chat_ding;			// sound widget for chat ding
var join_ding;			// sound widget for join ding
var leave_ding;			// sound widget for leave ding

////////////////
// Chat stuff //
////////////////
const file_chunks = {}; // msg_id => {chunks: [], received: 0, total: N}

////////////////////////////
// Send initial Login Cmd //
////////////////////////////
function try_login() {
   // Save the login user
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

function initialize_sounds() {
   // Attach the sound objects (chat/join/leave)
   chat_ding = document.getElementById('chat-ding');
   join_ding = document.getElementById('join-ding');
   leave_ding = document.getElementById('leave-ding');
   let default_sounds = localStorage.getItem('play_sounds');

   if (default_sounds === 'true') {
      $('button#bell-btn').data('checked', true);
   } else {
      $('button#bell-btn').data('checked', false);
   }

   let newSrc = !$('button#bell-btn').data('checked') ? 'img/bell-alert-outline.png' : 'img/bell-alert.png';
   $('img#bell-image').attr('src', newSrc);

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

   // Deal with volume change events
   $("input#alert-vol").on("change", function() {
      let volume = $(this).val() / 100;
      $("audio#chat-ding, audio#join-ding, audio#leave-ding").prop("volume", volume);
   });

   // Support toggling mute via bell button
   $('#bell-btn').click(function() { toggle_mute(); });
}

function toggle_mute() {
   $('button#bell-btn').data('checked', !$('#bell-btn').data('checked'));
   localStorage.setItem("play_sounds", $('button#bell-btn').data('checked'));

   let newSrc = $('button#bell-btn').data('checked') ? 'img/bell-alert-outline.png' : 'img/bell-alert.png';
   $('img#bell-image').attr('src', newSrc);
}

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
      // Stop HTML form submission
      evt.preventDefault();

      let user = $("input#user");
      let pass = $("input#pass");

      // Is the username field empty?
      if (user.val().trim() === "") {
         flash_red(user);
         user.focus();
         event.preventDefault();
         return;
      }

      // Is the password field empty?
      if (pass.val().trim() === "") {
         flash_red(pass);
         pass.focus();
         event.preventDefault();
         return;
      }

      // Since this is a manual reconnect attempt, unset ws_kicked which would block it
      ws_kicked = false;

      // we need to re-authenticate
      logged_in = false;

      // Open websocket connection
      ws_connect();

   });

   $('input#reset').click(function(evt) {
      console.log("Form reset");
   });

   // Set up sounds
   initialize_sounds();
   form_disable(true);

   // clear button
   $('#clear-btn').click(function() {
      $('#chat-input').val('');
      $('#chat-input').focus();
   });

   $('#chat-input').on('paste', function(e) {
      const items = (e.originalEvent || e).clipboardData.items;
      for (const item of items) {
         if (item.type.indexOf('image') !== -1) {
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
                  send_chunked_file(dataUrl);
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
            break;
         }
      }
   });

   // Toggle display of the emoji keyboard
   $('#emoji-btn').click(function() {
       const emojiKeyboard = $('#emoji-keyboard');
       if (emojiKeyboard.is(':visible')) {
           emojiKeyboard.hide(); // Hide if already visible
       } else {
           emojiKeyboard.show(); // Show if hidden
       }
   });

   // Send message to the server when "Send" button is clicked
   $('#send-btn').click(function() {
      var message = $('#chat-input').val().trim();
      var chat_msg = false;
      var msg_type = "invalid";

      // Determine if the message is a command, otherwise send it off as chat
      if (message) {
         if (message.charAt(0) == '/') {
//            console.log("got CHAT command:", message);
            var args = message.split(' ');

            // remove the leading /
            if (args.length > 0 && args[0].startsWith('/')) {
               args[0] = args[0].slice(1);
            }
            var command = args[0];

            // Compare the lower-cased command
            switch(command.toLowerCase()) {
               case 'ban':
                  console.log("Send BAN for", args[1]);
                  send_command(command, args[1]);
                  break;
               case 'clear':
                  console.log("Cleared scrollback");
                  clear_chatbox();
               case 'edit':
                  console.log("Send EDIT for", args[1]);
                  send_command(command, args[1]);
                  break;
               case 'help':
                  append_chatbox('<div><span class="error">HELP: All commands start with / and should be lower case</span></div>');
                  append_chatbox('<div><span class="error">/ban&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Ban a user from logging in</span></div>');
                  append_chatbox('<div><span class="error">/clear&nbsp;&nbsp;&nbsp;- Clear chat scrollback</span></div>');
                  append_chatbox('<div><span class="error">/edit&nbsp;&nbsp;&nbsp;&nbsp;- Edit a user</span></div>');
                  append_chatbox('<div><span class="error">/help&nbsp;&nbsp;&nbsp;&nbsp;- This help message</span></div>');
                  append_chatbox('<div><span class="error">/kick&nbsp;&nbsp;&nbsp;&nbsp;- Kick a user (with optional reason)</span></div>');
                  append_chatbox('<div><span class="error">/me&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Show as an ACTION in chat</span></div>');
                  append_chatbox('<div><span class="error">/mute&nbsp;&nbsp;&nbsp;&nbsp;- Mute a user, disables their TX and chat</span></div>');
                  append_chatbox('<div><span class="error">/whois&nbsp;&nbsp;&nbsp;- Show user information</span></div>');
                  break;
               case 'kick':
                  console.log("Sending KICK for", args[1]);
                  send_command(command, args[1]);
                  break;
               case 'me':	// /me shows an ACTION in the chat
                  message = message.slice(4);
                  console.log("ACTION ", message);
                  chat_msg = true;
                  msg_type = "action";
                  break;
               case 'mute':
                  console.log("Send MUTE", args[1]);
                  send_command(command, args[1]);
                  break;
               case 'whois':
                  console.log("Send WHOIS", args[1]);
                  send_command(command, args[1]);
                  break;
               default:
                  append_chatbox('<div><span class="error">Invalid command:' + command + '</span></div>');
                  break;
            }
         } else {
           chat_msg = true;
           msg_type = "pub";
         }

         // Is this a user message that we should display?
         if (chat_msg) {
            var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
            var msgObj = {
               "talk": {
                  "cmd": "msg",
                  "ts": my_ts,
                  "token": auth_token,
                  "msg_type": msg_type,
                  "data": message
               }
            };
            socket.send(JSON.stringify(msgObj));
         }

         // Clear the input field and after a delay re-focus it, to avoid flashing
         $('#chat-input').val('');
         setTimeout(function () {
              $('#chat-input').focus();
         }, 10);
      }
   });

   ///////////////////////
   // Keyboard controls //
   ///////////////////////
   $('#chat-input').keypress(function(e) {
      // ENTER
      if (e.which == 13) {
         $('#send-btn').click();
         e.preventDefault();
      }
   });

   // For clicks inside the document, do stuff
   $(document).click(function (event) {
      // If the click is NOT on an input, button, or a focusable element
      if (!$(event.target).is('#chat-input, a, [tabindex]')) {
         if (logged_in) {	// Focus the chat input field
            $('#chat-input').focus();
         } else {		// Nope, focus the user field
            $('form#login input#user').focus();
         }
      }
   });

   // Ensure #chat-box does not accidentally become focusable
   $('#chat-box').attr('tabindex', '-1');

   // User menu, close button
   $('#um-close').click(function() { $('#user-menu').hide(); });

   // Top menu
   $('span#tab-chat').click(function() {
       show_chat_window();
   });

   $('span#tab-rig').click(function() {
       $('div#win-chat').hide();
       $('div#win-settings').hide();
       $('div#win-rig').show();
   });

   $('span#tab-settings').click(function() {
       $('div#win-rig').hide();
       $('div#win-chat').hide();
       $('div#win-settings').show();
   });

   $('span#tab-dark').click(function() {
       toggle_dark_mode();
   });

   $('span#tab-logout').click(function() {
       logout();
   });

   // deal with our keypresses
   $(document).keydown(function (e) {
      if (e.key === "ArrowUp" || e.key === "ArrowDown" || e.key === "PageUp" || e.key === "PageDown") {
         e.preventDefault(); 	// Prevent default behavior (e.g., focusing or scrolling)

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
   $('#emoji-btn').prop('disabled', state);
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

async function sha1_hex(str) {
   const buffer = new TextEncoder().encode(str);
   const hashBuffer = await crypto.subtle.digest("SHA-1", buffer);
   return Array.from(new Uint8Array(hashBuffer))
      .map(b => b.toString(16).padStart(2, "0"))
      .join("");
}

async function authenticate(login_user, login_pass, auth_token, nonce) {
///////////////////////
// Replay protection //
///////////////////////

// XXX: Once the C side is implemented, we can re-enable this
//   var firstHash = await sha1_hex(login_pass);
//   var combinedString = firstHash + '+' + nonce;
//   var hashed_pass = await sha1_hex(combinedString);

   var hashed_pass = await sha1_hex(login_pass);  // Wait for the hash to complete
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

    // Natural, case-insensitive sort by name
    users.sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base', numeric: true }));

    users.forEach(user => {
       const { name, admin, tx, view_only, owner } = user;

       let badges = '';

       if (tx) {
          badges += '<span class="badge tx-badge">🔊</span>';
       }

       if (owner) {
          badges += '<span class="badge owner-badge">👑</span>';
       } else if (admin) {
          badges += '<span class="badge admin-badge">⭐</span>';
       } else if (view_only) {
          badges += '<span class="badge view-badge">👀</span>';
       } else {
          badges += '<span class="badge empty-badge">&nbsp;&nbsp;</span>';
       }

       const userItem = `<li>
          <span class="chat-user-list" onclick="show_user_menu('${user.name}')">
             ${badges}<span class="cul-self">${user.name}</span>
          </span>
       </li>`;

       $('#cul-list').append(userItem);
    });
}

function show_user_menu(username) {
    // Check if the current user has admin privileges (auth_privs contains 'admin.*')
    var isAdmin = /admin\..*/.test(auth_privs);

    // Admin menu to be appended if the user is an admin
    var admin_menu = `
        <hr width="50%"/>
        <li><button class="cul-menu-button" id="mute-user">Mute</button></li>
        <li><button class="cul-menu-button" id="kick-user">Kick</button></li>
        <li><button class="cul-menu-button" id="ban-user">Lock&Kick</button></li>
    `;

    // Base menu
    var user_email = 'sorry@not.yet';

    var menu = `
        <div id="um-header" style="position: relative;">
            <span id="um-close" style="position: absolute; top: 5px; right: 5px; cursor: pointer;">✖</span>
        </div>
        User: ${username}<br/>
        <span id="user-menu-items">
            <ul>
<!--                <li><a href="mailto:${user_email}" target="_blank">Email</a></li> -->
                <li><button class="cul-menu-button" id="whois-user">Whois</button></li>
                ${isAdmin ? admin_menu : ''} <!-- Append admin menu if the user is an admin -->
            </ul>
        </span>
    `;

    // Update the user menu and show it
    $('#user-menu').html(menu);
    $('#user-menu').show();

    // Attach event listeners to buttons using jQuery
    $('#whois-user').on('click', function() {
        send_command('whois', username);
    });

    $('#mute-user').on('click', function() {
        send_command('mute', username);
    });

    $('#kick-user').on('click', function() {
        send_command('kick', username);
    });

    $('#ban-user').on('click', function() {
        send_command('ban', username);
    });

    // Close button functionality
    $('#um-close').on('click', function() {
        $('#user-menu').hide();
    });
}

// Function to send commands over WebSocket
function send_command(cmd, target) {
   const msgObj = {
      "talk": {
         "cmd": cmd,
         "token": auth_token,  // assuming auth_token is available
         "target": target
      }
   };
   socket.send(JSON.stringify(msgObj));
   $('#user-menu').hide();
   console.log(`Sent command: /${cmd} ${target}`);
}

function msg_create_links(message) {
   return message.replace(
      /(https?:\/\/[^\s]+)/g,
      '<a href="$1" class="chat-link" target="_blank" rel="noopener">$1</a>'
   );
}

function ws_connect() {
   var rc = reconnecting;
   reconnecting = false;

   // Clear any previous reconnecting flag
   show_connecting(true);
   socket = new WebSocket(make_ws_url());

   if (ws_kicked == true) {
      // Prevent reconnecting for a moment at least
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
      append_chatbox('<div class="chat-status">' + my_ts + '&nbsp;WebSocket connected.</div>');
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
         append_chatbox('<div class="chat-status error">' + my_ts + '&nbsp;WebSocket error: ', error, 'occurred.</div>');
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

               // Linkify non-images
               if (msg_type === "file_chunk") {
//                  console.log("Handling image chunk: " + message);
                  handle_file_chunk(msgObj);
               } else if (msg_type === "action" || msg_type == "pub") {
                  // Conevert URLs to clickable links
                  message = msg_create_links(message);

                  // Don't play a bell or set highlight on SelfMsgs
                  if (sender === auth_user) {
                     if (msg_type === 'action') {
                        append_chatbox('<div>' + msg_ts + ' <span class="chat-my-msg-prefix">&nbsp;==>&nbsp;</span>***&nbsp;' + sender + '&nbsp;***&nbsp;<span class="chat-my-msg">' + message + '</span></div>');
                     } else {
                        append_chatbox('<div>' + msg_ts + ' <span class="chat-my-msg-prefix">&nbsp;==>&nbsp;</span><span class="chat-my-msg">' + message + '</span></div>');
                     }
                  } else {
                     if (msg_type === 'action') {
                        append_chatbox('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">&nbsp;' + sender + '&nbsp;</span>***&nbsp;<span class="chat-msg">' + message + '</span></div>');
                     } else {
                        append_chatbox('<div>' + msg_ts + ' <span class="chat-msg-prefix">&lt;' + sender + '&gt;&nbsp;</span><span class="chat-msg">' + message + '</span></div>');
                     }

                     // Play bell sound if the bell button is checked
                     if ($('#bell-btn').data('checked')) {
                        chat_ding.currentTime = 0;  // Reset audio to start from the beginning
                        chat_ding.play();
                     }

                     // Flash the indicator, if set
                     set_highlight("chat");

                     // XXX: Update the window title to show a pending message
                  }
               }
            } else if (cmd === 'join') {
               var user = msgObj.talk.user;

               if (user) {
                  var msg_ts = msg_timestamp(msgObj.talk.ts);
                  append_chatbox('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + user + '&nbsp;</span><span class="chat-msg">joined the chat</span>&nbsp;***</div>');
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
                  append_chatbox('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + user + '&nbsp;</span><span class="chat-msg">left the chat</span>&nbsp;***</div>');
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
               append_chatbox('<div><span class="error">' + my_ts + '&nbsp;Error: ' + error + '!</span></div>');
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
                  append_chatbox('<div><span class="msg-connected">' + my_ts + '&nbsp;***&nbspWelcome back, ' + auth_user + ', You have ' + auth_privs + ' privileges</span></div>');
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
                  append_chatbox('<div><span class="error">👽 Session expired, logging out</span></div>');
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
   append_chatbox('<div class="chat-status error">' + my_ts + '&nbsp; Reconnecting in ' + reconnect_delay + ' sec</div>');

   // Delay reconnecting for a bit
   reconnect_timer = setTimeout(function() {
      ws_connect();

      // increase delay for the next try
      reconnect_delay = reconnect_interval[Math.min(reconnect_interval.length - 1, reconnect_interval.indexOf(reconnect_delay) + 1)];
   }, reconnect_delay * 1000);
}

function make_ws_url() {
   var protocol = window.location.protocol === "https:" ? "wss://" : "ws://";
   return protocol + window.location.hostname + (window.location.port ? ":" + window.location.port : "") + "/ws/";
}

function flash_red(element) {
   // XXX: We should save the old border, if any, then restore it below
   element.focus()
   element.css("border", "2px solid red");
   setTimeout(() => {
      element.css("border", "");
   }, 2000);
}

function toggle_dark_mode() {
   const $darkTheme = $("#dark-theme");
   const $toggleBtn = $("#tab-dark");
   const darkCSS = "css/dark.css";

   let dark_mode = localStorage.getItem("dark_mode") !== "false"; 
   dark_mode = !dark_mode; // Toggle it

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

function show_login_window() {
   $('button#submit').prop('disabled', false);
   $('div#win-chat').hide();
   $('div#win-settings').hide();
   $('div#win-login').show();
   $('input#user').focus();
   $('div#tabstrip').hide();
//   $('.chroma-hash').show();
}

function show_chat_window() {
//   $('.chroma-hash').hide();
   $('div#win-login').hide();
   $('div#win-rig').hide();
   $('div#win-settings').hide();
   $('div#tabstrip').show();
   $('div#win-chat').show();
   $('#chat-input').focus();
}

function show_settings_window() {
//   $('.chroma-hash').hide();
   $('div#win-rig').hide();
   $('div#win-chat').hide();
   $('div#win-login').hide();
   $('div#tabstrip').show();
   $('div#win-settings').show();
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
   return `[${hh}:${mm}:${ss}]`;
}

function logout() {
   var msgObj = {
      "auth": {
         "cmd": "logout",
         "user": auth_user,
         "token": auth_token
      }
   };
   socket.send(JSON.stringify(msgObj));
}

function set_highlight(tab) {
   clear_highlight();

   // Add highlight to the specified tab
   $(`span#tab-${tab}`).addClass('chat-highlight');
}

function clear_highlight() {
   // Remove the highlight class from all tabs
   $('span[id^="tab-"]').removeClass('chat-highlight');
}

function send_chunked_file(base64Data, filename) {
   const chunkSize = 8000; // Safe under 64K JSON limit
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
            total_chunks: totalChunks, // Add total_chunks for all chunks
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

      append_chatbox($wrap.prop('outerHTML'));

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

function clear_chatbox() {
   $('#chat-box').empty();
}
