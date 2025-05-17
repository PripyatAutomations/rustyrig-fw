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
var ws_last_heard;		// When was the last time we heard something from the server? Used to send a keep-alive
var ws_last_pinged;
var ws_keepalives_sent = 0;
var ws_keepalive_time = 60;	// Send a keep-alive (ping) to the server every 60 seconds, if no other activity
var active_vfo;			// Active VFO

////////////////////////
// Latency calculator //
////////////////////////
let latency_samples = [];
var latency_timer;
const latency_max_samples = 50;

/////
/// chat stuff that needs to move
////
const UserCache = {
   users: {},

   add(user) {
      this.users[user.name] = {
         // Only copy fields that are actually present
         ...(user.hasOwnProperty('ptt')   && { ptt:   user.ptt }),
         ...(user.hasOwnProperty('muted') && { muted: user.muted }),
         ...(user.hasOwnProperty('privs') && { privs: user.privs })
      };
   },

   remove(name) {
      delete this.users[name];
   },

   update(user) {
      const existing = this.users[user.name];
      if (!existing) {
/*
         this.add({
            name: user.name,
            admin: user.is_admin,
            ptt: user.is_ptt,
            muted: user.is_muted
         });
*/
         this.add(user);
         return;
      }

      if ('is_admin' in user) existing.admin = user.is_admin;
      if ('is_ptt'   in user) existing.ptt   = user.is_ptt;
      if ('is_muted' in user) existing.muted = user.is_muted;
   },

   get(name) {
      return this.users[name] || null;
   },

   get_all() {
      return Object.entries(this.users).map(([name, props]) => ({ name, ...props }));
   },

   dump() {
      console.log("UserCache contents:");
      for (const [name, props] of Object.entries(this.users)) {
         console.log(`- ${name}:`, props);
      }
   }
};

// Support reloading the stylesheet (/reloadcss) without restarting the app
function reload_css() {
  $('link[rel="stylesheet"]').each(function() {
    let $link = $(this);
    let href = $link.attr('href').split('?')[0];
    $link.attr('href', href + '?_=' + new Date().getTime());
  });

   setTimeout(function() {
      let chatBox = $('#chat-box');
      chatBox.scrollTop(chatBox[0].scrollHeight);
   }, 250);
}

function user_link(username) {
   if (username === auth_user) {
      return `<a href="#" class="my-link" onclick="show_user_menu('${username.replace(/'/g, "\\'")}'); return false;">${username}</a>`;
   } else {
      return `<a href="#" class="other-link" onclick="show_user_menu('${username.replace(/'/g, "\\'")}'); return false;">${username}</a>`;
   }
}

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

function send_ping(sock) {
   if (ws_keepalives_sent > 3) {
     // XXX: Give up and reconnect
     console.log("We've reached 3 tries to send keepalive, giving up");
   }

   if (sock.readyState === WebSocket.OPEN) {
      const now = Math.floor(Date.now() / 1000);  // Unix time in seconds
      ws_keepalives_sent++;
      ws_last_pinged = now;
      sock.send("ping " + now);
   }
}

function make_ws_url() {
   var protocol = window.location.protocol === "https:" ? "wss://" : "ws://";
   return protocol + window.location.hostname + (window.location.port ? ":" + window.location.port : "") + "/ws/";
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

function ws_connect() {
   var rc = reconnecting;
   reconnecting = false;
   show_connecting(true);
   socket = new WebSocket(make_ws_url());

   // Was the websocket connection kicked? If so, don't reconnect
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
      var now = Math.floor(Date.now() / 1000);
      var my_ts = msg_timestamp(now);
      chat_append('<div class="chat-status">' + my_ts + '&nbsp;WebSocket connected.</div>');
      reconnecting = false; 		// Reset reconnect flag on successful connection
      reconnect_delay = 1; 		// Reset reconnect delay to 1 second

      // Set a timer to check if keepalives needs to be sent (every day 10 seconds)
      setInterval(function() {
         if (ws_last_heard < (now - ws_keepalive_time)) {
            console.log(`keep-alive needed; last-heard=${ws_lastheard} now=${now} keep-alive time: ${ws_keepalive_time}, sending`);
            // Send a keep-alive (ping) to the server so it will reply
            send_ping(socket);
         }
      }, 10000);
   };

   /* NOTE: On error sorts this out for us */
   socket.onclose = function() {
      if (typeof cul_offline === 'function') {
         cul_offline();
      }

      if (ws_kicked != true && reconnecting == false) {
         console.log("Auto-reconnecting ws (socket closed)");
         handle_reconnect();
      }
   };

   // When there's an error with the WebSocket
   socket.onerror = function(error) {
      cul_offline();

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
//      console.log("evt: ", event);
      var msgData = event.data;
      ws_last_heard = Date.now();

      try {
         var msgObj = JSON.parse(msgData);

         if (msgObj.syslog) {		// Handle syslog messages
            syslog_append(msgObj);
         } else if (msgObj.alert) {
            var alert_from = msgObj.alert.from.toUpperCase();
            var alert_ts = msgObj.alert.ts;
            var alert_msg = msgObj.alert.msg;
            var msg_ts = msg_timestamp(alert_ts);

            chat_append(`<div class="chat-status error">${msg_ts}&nbsp;!!ALERT!!&nbsp;&lt;${alert_from}&gt;&nbsp;&nbsp;${alert_msg}</div>`);
         } else if (msgObj.cat) {
//            console.log("msg: ", msgObj);
            var vfo = msgObj.cat.state.vfo;
            var freq = msgObj.cat.state.freq;
            var mode = msgObj.cat.state.mode;
            var width = msgObj.cat.state.width;
            var ptt = msgObj.cat.state.ptt;
            var ts = msgObj.cat.ts;

            if (typeof freq !== 'undefined') {
               if (vfo === "A") {
                  $('span#vfo-a-freq').html(freq);
               } else if (vfo === "B") {
                  $('span#vfo-b-freq').html(freq);
               }
               let $input = $('#rig-freq');
               freq_set_digits(freq, $input);
            }
            if (typeof mode !== 'undefined') {
               if (vfo === "A") {
                  $('span#vfo-a-mode').html(mode);
               } else if (vfo === "B") {
                  $('span#vfo-b-mode').html(mode);
               }
            }
            if (typeof width !== 'undefined') {
               if (vfo === "A") {
                  $('span#vfo-a-width').html(width);
               } else if (vfo === "B") {
                  $('span#vfo-b-width').html(width);
               }
            }
            if (typeof ptt !== 'undefined') {
               var ptt_l = ptt.toLowerCase();

               if (ptt_l === "true" || ptt_l === "on" || ptt_l === 'yes') {
                  $('#rig-ptt').addClass("red-btn");
               } else {
                  $('#rig-ptt').removeClass("red-btn");
               }
            }
         } else if (msgObj.ping) {			// Handle PING messages
            var ts = msgObj.ping.ts;
            if (typeof ts === 'undefined' || ts <= 0) {
               // Invalid timestamp in the ping, ignore it
               return false;
            }
//            console.log("Got PING from server with ts", ts, "replying!");
            var newMsg = { pong: { ts: String(ts) } };
            socket.send(JSON.stringify(newMsg));
         } else if (msgObj.talk) {		// Handle Chat messages
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
               console.log("join msg: ", msgObj);
               var user = msgObj.talk.user;
               var privs = msgObj.talk.privs;

               if (typeof user !== 'undefined') {
                  var msg_ts = msg_timestamp(msgObj.talk.ts);
                  var nl = user_link(user);

                  UserCache.add({ name: user, ptt: false, muted: false, privs: privs });

                  chat_append('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + nl + '&nbsp;</span><span class="chat-msg">joined the chat</span>&nbsp;***</div>');
                  // Play join (door open) sound if the bell button is checked
                  if ($('#bell-btn').data('checked')) {
                     if (!(user === auth_user)) {
                        join_ding.currentTime = 0;  // Reset audio to start from the beginning
                        join_ding.play();
                     }
                  }
                  console.log("Join by:", user);
               } else {
                  console.log("got join for undefined user, ignoring");
               }
            } else if (cmd === 'kick') {
               console.log("kick msg: ", msgObj);
               // Play leave (door close) sound if the bell button is checked
               if ($('#bell-btn').data('checked')) {
                  if (!(user === auth_user)) {
                     leave_ding.currentTime = 0;  // Reset audio to start from the beginning
                     leave_ding.play();
                  }
               }
               UserCache.remove(user);
               console.log("Kick command received for user: ", user, " reason: ", msgObj.talk.data.reason);
            } else if (cmd === 'mute') {
               // XXX: If this mute is for us, disable the send button
               // XXX: and show an alert.
               console.log("Mute command received for user: ", user);
               UserCache.update({ name: user, is_muted: true });
            } else if (cmd === 'whois') {
               const clones = msgObj.talk.data;

               if (!clones || clones.length === 0) {
                  return;
               }
               form_disable(true);

               const info = clones[0]; // shared info from the first entry

               let html = `<strong>User:</strong>&nbsp;${info.username}<br>`;
               html += `<strong>Email:</strong>&nbsp;${info.email}<br>`;
               html += `<strong>Privileges:</strong>&nbsp;${info.privs || 'None'}<br>`;
               html += '<hr width="75%"/>';

               html += `<strong>Active Sessions: ${clones.length}</strong><br>`;
               clones.forEach((session) => {
                  let clone_num = session.clone + 1;
                     html += `&nbsp;&nbsp;<em>Clone #${clone_num}</em><br>`;
                  html += `&nbsp;&nbsp;&nbsp;&nbsp;<strong>Connected:</strong> ${new Date(session.connected * 1000).toLocaleString()}`;
                  html += `&nbsp;&nbsp;<strong>Last Heard:</strong> ${new Date(session.last_heard * 1000).toLocaleString()}<br>`;
                  html += `&nbsp;&nbsp;&nbsp;&nbsp;<strong>User-Agent:</strong> <code>${session.ua}</code><br><br>`;
               });

               html += "<hr/><br/>Click window or hit escape to close";
               $('#chat-whois').html(html).show('slow');
            } else if (cmd === "quit") {
               console.log("quit msg: ", msgObj);
               var user = msgObj.talk.user;
               var reason = msgObj.talk.reason;

               if (user) {
                  var msg_ts = msg_timestamp(msgObj.talk.ts);
                  if (typeof reason === 'undefined') {
                     reason = 'Client exited';
                  }

                  UserCache.remove(user);
                  chat_append('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + user + '&nbsp;</span><span class="chat-msg">disconnected: ' + reason + '</span>&nbsp;***</div>');
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
               parse_names_reply(msgObj);
            } else if (cmd === "unmute") {
               // XXX: If unmute is for us, enable send button and let user know they can talk again
               UserCache.update({ name: user, is_muted: false });
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
               stop_reconnecting();			// disable auto-reconnects

               console.log("auth.error: ", error);
               var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
               chat_append('<div><span class="error">' + my_ts + '&nbsp;Error: ' + error + '!</span></div>');
               show_login_window();
               $('span#sub-login-error-msg').empty();
               $('span#sub-login-error-msg').append("<span>", error, "</span>");
               form_disable(true);

               // Get rid of message after about 30 seconds XXX: disabled for now, add a check if /kicked and dont timeout
//               setTimeout(function() { $('div#sub-login-error').hide(); }, 30000);

               $('div#sub-login-error').show();
               $('button#login-err-ok').click(function() {
                  form_disable(false);
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

                  // Clear the chat window if changing users
                  if (login_user !== "GUEST" && auth_user !== login_user.toUpperCase()) {
                     chatbox_clear();
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
            console.log("Got unknown message from server: ", msgObj);
         }
      } catch (e) {
         console.error("Error parsing message:", e);
         console.log("Unknown data: ", msgObj);
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
   element.focus()
   var old_border = element.css("border");
   element.css("border", "2px solid red");
   setTimeout(() => {
      let restore_border = old_border;
      element.css("border", restore_border);
   }, 1000);
}

function set_dark_mode(state) {
   let current = localStorage.getItem("dark_mode") !== "false";
   let dark_mode = (typeof state === 'undefined') ? !current : state;

   const $toggleBtn = $("#tab-dark");

   $('link[id^="css-"]').filter(function() {
      return this.id.endsWith("-dark");
   }).each(function() {
      if (dark_mode) {
         $(this).removeAttr("disabled");
      } else {
         $(this).attr("disabled", "disabled");
      }
   });

   $toggleBtn.text(dark_mode ? "light" : "dark");
   console.log("Set " + (dark_mode ? "Dark" : "Light") + " Mode");

   localStorage.setItem("dark_mode", dark_mode ? "true" : "false");
}

function form_disable(state) {
   if (state === false) {
      $('#button-box, #chat-input').show(300);
      $('#chat-input').focus();
   } else {
      $('#button-box, #chat-input').hide(250);
   }
}


function latency_send_pings(socket) {
   const now = Date.now();
   socket.send(JSON.stringify({type: 'ping', ts: now}));
}

function latency_check_init() {
   socket.onmessage = function(event) {
      let msg = JSON.parse(event.data);

      if (msg.type === 'pong' && msg.ts) {
         let rtt = Date.now() - msg.ts;
         latency_samples.push(rtt);

         if (latency_samples.length > latency_max_samples) {
            latency_samples.shift();
         }
      }
   };
}

function latency_toggle_check() {
   if (!latency_timer) {
      latency_timer = setInterval(latency_send_pings, 2000);
   } else {
      // Destroy the timer instance
      
   }
}

function latency_get_avg() {
   if (latency_samples.length === 0) {
      return null;
   }

   let sum = latency_samples.reduce((a, b) => a + b, 0);
   return sum / latency_samples.length;
}


if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_init() {
   // try to prevent submitting a GET for this
   $(document).on('submit', 'form', function(e) {
      e.preventDefault();
   });

   /////////////////////////////////////
   // Load settings from LocalStorage //
   /////////////////////////////////////
   // bind tab strip handlers
   $('span#tab-chat').click(function() {
      show_chat_window();
   });
   $('span#tab-rig').click(function() { show_rig_window(); });
   $('span#tab-config').click(function() { show_config_window(); });
   $('span#tab-syslog').click(function() { show_syslog_window(); });
   $('span#tab-dark').click(function() {  
      var dark_mode = localStorage.getItem("dark_mode") !== "false"
      set_dark_mode(!dark_mode);
   });
   $('span#tab-logout').click(function() { logout(); });
   chat_append('<div><span class="error">***** New commands are available! See /help for chat help and !help for rig commands *****</span></div>');

   // Reset buttons
   $('#login-reset-btn').click(function(evt) {
      console.log("Form reset");
      $('input#user, input#pass').val('');
   });

   form_disable(true);

   // Toggle display of the emoji keyboard
   $('#emoji-btn').click(function() {
      const emojiKeyboard = $('#emoji-keyboard');
      if (emojiKeyboard.is(':visible')) {
         emojiKeyboard.hide('slow');
      } else {
         emojiKeyboard.show('fast');
      }
   });

   // Native listener for ctrl + wheel
   document.addEventListener('wheel', function(e) {
      if (e.ctrlKey) {
         e.preventDefault();
      }
   }, { passive: false });

   $('button#reload-css').click(reload_css);
   $(document).on('keydown', function(e) {
      // Handle login field focus transition
      if (document.activeElement.matches('form#login input#user')) {
         if ((e.key === 'Enter') || (e.key === 'Tab' && !e.shiftKey)) {
            e.preventDefault();
            document.querySelector('form#login input#pass')?.focus();
            return;
         }
      }

      const inputFocused = document.activeElement.id === 'chat-input';

      // Prevent zooming in/out
      if (e.ctrlKey && (
            e.key === '+' || e.key === '-' || 
            e.key === '=' || e.key === '0' || 
            e.code === 'NumpadAdd' || e.code === 'NumpadSubtract')) {
         e.preventDefault();
      } else if (!inputFocused && e.key === '/' && !e.ctrlKey && !e.metaKey) {
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
      } else {
         return;
      }
      form_disable(false);
   });

   $(document).click(function (e) {
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
/*
      } else if (logged_in) {	// Focus the chat input field
            $('#chat-input').focus();
         } else {		// Nope, focus the user field
            $('form#login input#user').focus();
         }
*/
      }
   });

   let completing = false;
   let completionList = [];
   let completionIndex = 0;
   let matchStart = 0;
   let matchLength = 0;

   function getCULNames() {
      return $('#cul-list .chat-user-list .cul-self').map(function () {
         return $(this).text();
      }).get();
   }

   function updateCompletionIndicator(name) {
      if (name) {
         $('#completion-indicator').text(`🔍 COMPLETING: ${name}`).show();
      } else {
         $('#completion-indicator').hide();
      }
   }

   $('#chat-input').on('keydown', function (e) {
      const input = $(this);
      const text = input.val();
      const caretPos = this.selectionStart;

      if (e.key === 'Tab') {
         e.preventDefault();
/*
         if (!completing) {
            const beforeCaret = text.slice(0, caretPos);
            const match = beforeCaret.match(/@(\w*)$/);

            if (match) {
               const word = match[1];
               matchStart = beforeCaret.lastIndexOf(word);
               matchLength = word.length;
               const afterCaret = text.slice(caretPos);

               completionList = getCULNames().filter(name => name.toLowerCase().startsWith(word.toLowerCase()));

               if (!completionList.length) {
                  return;
               }

               completing = true;
               completionIndex = 0;

               const current = completionList[completionIndex];
               const completed = text.slice(0, matchStart + 1) + current + afterCaret;

               input.val(completed);
               const newCaret = matchStart + 1 + current.length;
               this.setSelectionRange(newCaret, newCaret);
               updateCompletionIndicator(current);
               completionIndex = (completionIndex + 1) % completionList.length;
            }
         }
         if (completing && completionList.length) {
            const currentName = completionList[completionIndex];
            const atStart = matchStart === 0;
            const completedText = '@' + 
               text.slice(0, matchStart) + currentName + (atStart ? ": " : " ") + text.slice(caretPos);

            input.val(completedText);
            const newCaret = matchStart + currentName.length + (atStart ? 2 : 1);
            this.setSelectionRange(newCaret, newCaret);

            updateCompletionIndicator(currentName);
            completionIndex = (completionIndex + 1) % completionList.length;
         }
      } else if (e.key === 'Escape') {
         if (completing) {
            input.val(originalText);
            this.setSelectionRange(matchStart + 1 + originalPrefix.length, matchStart + 1 + originalPrefix.length);
            completing = false;
            updateCompletionIndicator(null);
         }
      } else if (completing && !e.key.match(/^[a-zA-Z0-9]$/)) {
         completing = false;
         updateCompletionIndicator(null);
*/
      } else if (e.key === "ArrowUp" || e.key === "ArrowDown" || e.key === "PageUp" || e.key === "PageDown") {
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
      } else if (completing && (e.key === ' ' || e.key === 'Enter')) {
  /*
         // Finalize current match
         const finalName = completionList[(completionIndex - 1 + completionList.length) % completionList.length];
         const finalizedText =
            text.slice(0, matchStart + 1) + finalName + text.slice(caretPos);
         input.val(finalizedText);

         const newCaret = matchStart + 1 + finalName.length;
         this.setSelectionRange(newCaret, newCaret);
         completing = false;
         updateCompletionIndicator(null);
*/
      }
   });

   // Handle the vertical height used by keyboard on mobile - xxx fix this!
//   function viewport_resize() {
//      let vh = window.innerHeight * 0.01;
//      $(':root').css('--vh', vh + 'px');
//   }
//   $(window).on('resize orientationchange', viewport_resize);
//   $(document).ready(viewport_resize);
});
