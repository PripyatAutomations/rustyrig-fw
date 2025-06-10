//////////////////////////
// Socket related stuff //
//////////////////////////
var socket;
var ws_kicked = false;		// were we kicked? stop autoreconnects if so
let reconnecting = false;
let reconnect_delay = 1;
let reconnect_interval = [1, 2, 5, 10, 30, 60 ];
var reconnect_timer;  		// so we can stop reconnects later
var reconnect_tries = 0;	// how many times have we tried to connect?
var max_reconnects = 10;	// maximum times we'll retry connecting
var ws_last_heard;		// When was the last time we heard something from the server? Used to send a keep-alive
var ws_last_pinged;
var ws_keepalives_sent = 0;
var ws_keepalive_time = 60;	// Send a keep-alive (ping) to the server every 60 seconds, if no other activity

var ChatBox;

function msg_timestamp(msg_ts) {
   if (typeof msg_ts !== "number") {
      msg_ts = Number(msg_ts); 			// Convert string to number if necessary

      if (isNaN(msg_ts)) {
         return "&nbsp;&nbsp;&nbsp;&nbsp;:&nbsp;&nbsp;&nbsp;&nbsp;";
      }
   }

   let date = new Date(msg_ts * 1000); 		// Convert seconds to milliseconds
   let hh = String(date.getHours()).padStart(2, '0');
   let mm = String(date.getMinutes()).padStart(2, '0');
   let ss = String(date.getSeconds()).padStart(2, '0');
   return `[${hh}:${mm}:${ss}]`;
}

function send_ping(sock) {
   if (ws_keepalives_sent > 3) {
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

function ws_connect() {
   var rc = reconnecting;
   reconnecting = false;
   show_connecting(true);

   // destroy old socket, if present
   if (typeof socket !== 'undefined') {
      socket.close();
      socket = null;
   }

   socket = new WebSocket(make_ws_url());
   socket.binaryType = "arraybuffer";
   // Was the websocket connection kicked? If so, don't reconnect
   if (ws_kicked == true) {
      console.log("Preventing auto-reconnect - we were kicked");
      return;
   }

   socket.onmessage = function(event) {
       webui_handle_ws_msg(event);
   }

   socket.onopen = function() {
      ws_kicked = false;
      show_connecting(false);
      try_login();
      form_disable(false);
      var now = Math.floor(Date.now() / 1000);
      var my_ts = msg_timestamp(now);
      ChatBox.Append('<div class="chat-status">' + my_ts + '&nbsp;WebSocket connected.</div>');
      reconnecting = false; 		// Reset reconnect flag on successful connection
      reconnect_delay = 1; 		// Reset reconnect delay to 1 second

      UserCache.clear();

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
      console.warn("WebSocket closed", {
         code: event.code,         // 1006 means abnormal close
         reason: event.reason,     // only non-empty if server sent one
         wasClean: event.wasClean  // false if closed due to error
      });

      if (typeof cul_offline === 'function') {
         cul_offline();
      }

//      WebUiAudio.flushPlayback();
      if (ws_kicked != true && reconnecting == false) {
         console.log("Auto-reconnecting ws (socket closed)");
         handle_reconnect();
      }
   };

   // When there's an error with the WebSocket
   socket.onerror = function(error) {
      console.log("Socket error!", error);
      socket.close();
      cul_offline();

      var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
      if (rc === false) {
         ChatBox.Append('<div class="chat-status error">' + my_ts + '&nbsp;WebSocket error: ', error, 'occurred.</div>');
      }

      if (ws_kicked != true && reconnecting == false) {
         console.log("Auto-reconnecting ws (on-error)");
         handle_reconnect();
      }
//      WebUiAudio.flushPlayback();
   };
}

function handle_binary_frame(event) {
      if (event.data instanceof ArrayBuffer) {
         playAudioPacket(event.data, audio_codec);
      } else {
         console.log("Invalid binary frame (not ArrayBuffer)");
      }
}

function webui_handle_ws_msg(event) {
   if (event.data instanceof ArrayBuffer) {
      handle_binary_frame(event);
   } else if (typeof event.data === "string") {
  // console.log("evt:", event);
      var msgData = event.data;
 //     console.log("Got string:", msgData);
      ws_last_heard = Date.now();

      try {
         var msgObj = JSON.parse(msgData);

         if (msgObj.syslog) {		// Handle syslog messages
            syslog_append(msgObj);
         } else if (msgObj.error) {
            console.log("ERR:", msgObj);
            var msg = msgObj.error;
            ChatBox.Append(`<div class="chat-status notice">ERROR: ${msg}</div>`);
            console.log("NOTICE:", msg);
         } else if (msgObj.hello) {
            ChatBox.Append(`<div class="chat-status notice">Server version: ${msgObj.hello} using codec ${msgObj.codec} at ${msgObj.rate / 1000}Khz</div>`);
            if (msgObj.rate) {
               audio_rate = msgObj.rate;
            }
            if (msgObj.codec) {
               audio_codec = msgObj.codec;
            }
         } else if (msgObj.alert) {
            var alert_from = msgObj.alert.from.toUpperCase();
            var alert_ts = msgObj.alert.ts;
            var alert_msg = msgObj.alert.msg;
            var msg_ts = msg_timestamp(alert_ts);

            if (alert_from === '***SERVER***') {
               alert_from = '';
            } else {
               alert_from = '&nbsp;&lt;' + alert_from + '&gt;&nbsp;';
            }

            ChatBox.Append(`<div class="chat-status error">${msg_ts}&nbsp;!!ALERT!!${alert_from}&nbsp;${alert_msg}</div>`);
         } else if (msgObj.cat) {
 //           console.log("CAT msg:", msgObj);
            webui_parse_cat_msg(msgObj);
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
            webui_parse_chat_msg(msgObj);
         } else if (msgObj.log) {
            var data = msgObj.log.data;
            // XXX: show in log window
            console.log("log message:", data);
         } else if (msgObj.auth) {
            webui_parse_auth_msg(msgObj);
         } else {
            console.log("Got unknown message from server:", msgObj);
         }
      } catch (e) {
         console.error("Error parsing message:", e);
         console.log("Unknown data:", msgObj);
      }
   } else {
      console.warn("Unknown message type:", event.data);
   }

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
   reconnect_tries = 0;
}

function handle_reconnect() {
   if (reconnecting) {
      return;
   }

   reconnecting = true;
   reconnect_tries++;
   show_connecting(true);

   var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
   ChatBox.Append('<div class="chat-status error">' + my_ts + '&nbsp; Reconnecting in ' + reconnect_delay + ' sec (attempt ' + reconnect_tries + '/' + max_reconnects + ')</div>');

   if (reconnect_tries >= max_reconnects) {
      ChatBox.Append('<div class="chat-status error">' + my_ts + '&nbsp; Giving up on reconnecting after ' + reconnect_tries + ' attempts!</div>');
      stop_reconnecting();
      wm_switch_tab('login');
   }

   // Delay reconnecting for a bit
   reconnect_timer = setTimeout(function() {
      ws_connect();

      // increase delay for the next try
      reconnect_delay = reconnect_interval[Math.min(reconnect_interval.length - 1, reconnect_interval.indexOf(reconnect_delay) + 1)];
   }, reconnect_delay * 1000);
}

function set_dark_mode(state) {
   let current = localStorage.getItem("dark_mode") !== "false";
   let dark_mode = (typeof state === 'undefined') ? !current : state;

   const $dark_btn = $("#tab-dark");

   $('link[id^="css-"]').filter(function() {
      return this.id.endsWith("-dark");
   }).each(function() {
      if (dark_mode) {
         $(this).removeAttr("disabled");
      } else {
         $(this).attr("disabled", "disabled");
      }
   });

   $dark_btn.text(dark_mode ? "light" : "dark");
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

if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_init() {
   // try to prevent submitting a GET for this
   $(document).on('submit', 'form', function(e) {
      e.preventDefault();
   });

   // bind tab strip handlers
   $('span#tab-chat').click(function() { wm_switch_tab('chat'); });
   $('span#tab-rig').click(function() { wm_switch_tab('rig'); });
   $('span#tab-config').click(function() { wm_switch_tab('cfg'); });
   $('span#tab-syslog').click(function() { wm_switch_tab('syslog'); });

   $('span#tab-dark').click(function() {  
      var dark_mode = localStorage.getItem("dark_mode") !== "false"
      set_dark_mode(!dark_mode);
   });

   let chatBox = $('#chat-box');
   ChatBox = new WebUiChat(chatBox);

   $('span#tab-logout').click(function() { logout(); });
   ChatBox.Append('<div><span class="error">***** New commands are available! See /help for chat help and !help for rig commands *****</span></div>');

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

   $('button#reload-css').click(reload_css);

// XXX: Hook this up so it can send a dying gasp to release the session quicker
//   $(window).on('unload', function() {
//      navigator.sendBeacon('/disconnect', JSON.stringify({token: auth_token }));
//   });
});
