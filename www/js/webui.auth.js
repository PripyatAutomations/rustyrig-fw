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

      $('button#login-submit-btn').prop('disabled', false);

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
// Authentication/Session //
////////////////////////////
var logged_in;			// Did we get an AUTHORIZED response?
var auth_user;			// Username the server sent us
var auth_token;			// Session token the server gave us during LOGIN
var auth_privs;			// Privileges the server has granted us
var remote_nonce;		// The login nonce, which is used to derive a replay protected password response
var login_user;			// Username we send to the server

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

   wmSwitchTab('login');
}
