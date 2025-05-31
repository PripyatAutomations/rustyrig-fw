////////////////////////////
// Authentication/Session //
////////////////////////////
var logged_in;			// Did we get an AUTHORIZED response?
var auth_user;			// Username the server sent us
var auth_token;			// Session token the server gave us during LOGIN
var auth_privs;			// Privileges the server has granted us
var remote_nonce;		// The login nonce, which is used to derive a replay protected password response
var login_user;			// Username we send to the server

if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_auth_init() {
   $('input#user').change(function() {
      // Cache the username and force to upper case
      login_user = $('input#user').val().toUpperCase();
      $('input#user').val = login_user;
   });

   // When the form is submitted, we need to send the username and wait for a nonce to come back
   $('form#login').submit(function(evt) {
      // Stop HTML form submission
      evt.preventDefault();

      // disable the button
      $('button#login-submit-btn').prop('disabled', true);

      let user = $("input#user");
      let pass = $("input#pass");

      // A username is *required*
      if (user.val().trim() === "") {
         flash_red(user);
         user.focus();
         event.preventDefault();
         return;
      }

      // A password is *required*
      if (pass.val().trim() === "") {
         flash_red(pass);
         pass.focus();
         event.preventDefault();
         return;
      }

      // Since this is a manual reconnect attempt, unset ws_kicked which would block it
      ws_kicked = false;
      logged_in = false;
      ws_connect();
   });
// If we can fix the positioning, this is nice to have... but disabled for now
//      var chroma_hash = $("input:password").chromaHash({ bars: 4, minimum: 3, salt:"63d38fe86e1ea020d1dc945a10664d80" });
   $('#win-login input#user').focus();

   $(window).on('beforeunload', function() {
       logout();
   }); 
});

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
   // Here we hash the user-supplied password,
   // add the nonce the server sent, then sha1sum it again
   var firstHash = await sha1_hex(login_pass);
   var combinedString = firstHash + '+' + nonce;
   var hashed_pass = await sha1_hex(combinedString);

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

function logout() {
   if (typeof socket !== 'undefined' && socket.readyState === WebSocket.OPEN) {
      var msgObj = {
         "auth": {
            "cmd": "logout",
            "user": auth_user,
            "token": auth_token
         }
      };
      socket.send(JSON.stringify(msgObj));
   }

   if (typeof chatbox_clear === "function") {
      chatbox_clear();
   }

   if (typeof syslog_clear === "function") {
      syslog_clear();
   }

   wm_switch_tab('login');
}

////////////////////////
////////////////////////
////////////////////////
function webui_parse_auth_msg(msgObj) {
   var cmd = msgObj.auth.cmd;
   var error = msgObj.auth.error;

   if (error) {
      var error = msgObj.auth.error;
      stop_reconnecting();			// disable auto-reconnects

      console.log("auth.error:", error);
      var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
      ChatBox.Append('<div><span class="error">' + my_ts + '&nbsp;Error: ' + error + '!</span></div>');
      wm_switch_tab('login');
      $('span#sub-login-error-msg').empty();
      $('span#sub-login-error-msg').append("<span>", error, "</span>");
      form_disable(true);

      // Get rid of message after about 30 seconds XXX: disabled for now, add a check if /kicked and dont timeout
   //   setTimeout(function() { $('div#sub-login-error').hide(); }, 30000);

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
         wm_switch_tab(active_tab);
         var my_ts = msg_timestamp(Math.floor(Date.now() / 1000));
         ChatBox.Append('<div><span class="msg-connected">' + my_ts + '&nbsp;***&nbspWelcome back, ' + auth_user + ', You have ' + auth_privs + ' privileges</span></div>');
         Audio = new WebUiAudio();
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
         ChatBox.Append('<div><span class="error">Session expired, logging out</span></div>');
         wm_switch_tab('login');
         break;
      default:
         console.log("Unknown auth command:", cmd);
         break;
   }
}
