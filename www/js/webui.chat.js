////////////////
// Chat Stuff //
////////////////
function msg_create_links(message) {
   return message.replace(
      /(https?:\/\/[^\s]+)/g,
      '<a href="$1" class="chat-link" target="_blank" rel="noopener">$1</a>'
   );
}

function chat_append(msg) {
   $('#chat-box').append(msg);
   setTimeout(function () {
      $('#chat-box').scrollTop($('#chat-box')[0].scrollHeight);
   }, 10);
};

function clear_chatbox() {
   $('#chat-box').empty();

   setTimeout(function () {
      $('#chat-box').scrollTop($('#chat-box')[0].scrollHeight);
   }, 10);
}

function insert_at_cursor($input, text) {
   const start = $input.prop('selectionStart');
   const end = $input.prop('selectionEnd');
   const val = $input.val();
   $input.val(val.substring(0, start) + text + val.substring(end));
   $input[0].setSelectionRange(start + text.length, start + text.length);
}

function paste_plaintext(e, item) {
   item.getAsString(function(text) {
      const $input = $('#chat-input');
      const start = $input.prop('selectionStart');
      const end = $input.prop('selectionEnd');
      const val = $input.val();
      insert_at_cursor($('#chat-input'), text);
   });
}

function paste_html(e, item) {
   item.getAsString(function(html) {
      const text = $('<div>').html(html).text();
      insert_at_cursor($('#chat-input'), text);
   });
}

function handle_paste(e) {
   const items = (e.originalEvent || e).clipboardData.items;

   for (const item of items) {
      if (item.type.indexOf('image') !== -1) {
         paste_image(e, item);
         break;
      } else if (item.type === 'text/plain') {			// Handle plaintext
         paste_plaintext(e, item);
         break;
      } else if (item.type === 'text/html') {			// Strip HTML tabs
         paste_html(e, item);
         break;
      } else {
         alert("PASTE: Unsupported file type: " + item.type);
         console.log("PASTE: Unsupported file type: ", item.type);
         break;
      }
   }
}

function chat_init() {
/*
 * XXX: This needs to check the active tab first
 *
   $('#chat-link').click(function(e) {
      if (logged_in) {
         $('#chat-input').focus();
      }
   });
*/
   $('#chat-input').on('paste', function(e) {
      handle_paste(e);
      e.preventDefault();
   });

   $('#send-btn').click(function(e) {
      parse_chat_cmd(e);
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

   // Ensure #chat-box does not accidentally become focusable
   $('#chat-box').attr('tabindex', '-1');
   $('#um-close').click(function() {
      form_disable(false);
      $('#user-menu').hide('slow');
   });
   $('span#tab-chat').click(function() { show_chat_window(); });
   $('span#tab-rig').click(function() { show_rig_window(); });
   $('span#tab-config').click(function() { show_config_window(); });
   $('span#tab-syslog').click(function() { show_syslog_window(); });
   $('span#tab-dark').click(function() { toggle_dark_mode(); });
   $('span#tab-logout').click(function() { clear_chatbox(); logout(); });
}

function cul_offline() {
   // Clear the user-info cache (populated from JOIN messages)
   user_cache = {};

   $('#cul-list').empty();
   $('#cul-list').append('<span class="error">OFFLINE</span>');
}

function cul_update(message) {
    $('#cul-list').empty();
    const users = message.talk.users;

    // Only show each user once in the list
    const uniqueUsers = Array.from(
       new Map(users.map(u => [u.name.toLowerCase(), u])).values()
    );

    uniqueUsers.sort((a, b) => a.name.localeCompare(b.name, undefined, {
       sensitivity: 'base',
       numeric: true
    }));

    uniqueUsers.forEach(user => {
       const { name, admin, tx, view_only, owner } = user;

       let badges = '', tx_badges = '';

       if (tx) {
          tx_badges += '<span class="badge tx-badge">🔊</span>';
       }

       if (owner) {
          badges += '<span class="badge owner-badge">👑&nbsp;</span>';
       } else if (admin) {
          badges += '<span class="badge admin-badge">⭐&nbsp;</span>';
       } else if (view_only) {
          badges += '<span class="badge view-badge">👀&nbsp;</span>';
       } else {
          badges += '<span class="badge empty-badge">&nbsp;✴&nbsp;</span>';
       }

       const userItem = `<li>
          <span class="chat-user-list" onclick="show_user_menu('${user.name}')">
             ${badges}<span class="cul-self">${user.name}</span>${tx_badges}
          </span>
       </li>`;

       $('#cul-list').append(userItem);
    });
}

function send_admin_command(cmd, username) {
   const needsReason = ['kick', 'ban', 'mute'];

   if (needsReason.includes(cmd)) {
      show_reason_modal(cmd, username);
   } else {
      chat_send_command(cmd, { target: username });
   }
}

function show_reason_modal(cmd, username) {
   const modal = document.getElementById("reason-modal");
   const form = document.getElementById("reason-form");
   const textarea = document.getElementById("reason-text");
   const title = document.getElementById("reason-title");

   title.textContent = `Enter reason for ${cmd}ing ${username}`;
   textarea.value = "";

   modal.style.display = "block";
   textarea.focus();

   const handler = function(e) {
      e.preventDefault();
      const reason = textarea.value.trim();
      if (reason) {
         chat_send_command(cmd, { target: username, reason: reason });
      }
      modal.style.display = "none";
      form_disable(false);
      form.removeEventListener("submit", handler);
   };

   form.addEventListener("submit", handler);
}

function show_user_menu(username) {
    var isAdmin = /admin/.test(auth_privs);

    form_disable(true);

    // Admin menu to be appended if the user is an admin
    var admin_menu = `
        <hr width="50%"/>
        <li><button class="cul-menu-button" id="mute-user">Mute</button></li>
        <li><button class="cul-menu-button" id="kick-user">Kick</button></li>
        <li><button class="cul-menu-button" id="ban-user">Lock&Kick</button></li>
    `;

    // Base menu
    // We need to extract this from the user-cache
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
                ${isAdmin ? admin_menu : ''}
            </ul>
        </span>
    `;

    // Update the user menu and show it
    $('#user-menu').html(menu);
    $('#user-menu').show();

    // Attach event listeners
    $('#whois-user').on('click', () => chat_send_command('whois', { target: username }));
    $('#mute-user').on('click', () => chat_send_command('mute', { target: username }));
    $('#kick-user').on('click', () => send_admin_command('kick', username));
    $('#ban-user').on('click', () => send_admin_command('ban', username));

    // Close button functionality
    $('#um-close').on('click', function() {
        form_disable(false);
        $('#user-menu').hide('slow');
    });
}

// Function to send commands over WebSocket
function chat_send_command(cmd, args) {
   const msgObj = {
      "talk": {
         "cmd": cmd,
         "token": auth_token,
         "args": args
      }
   };

   var msgObj_j = JSON.stringify(msgObj);
   socket.send(msgObj_j);
   $('#user-menu').hide();
   console.log("Sent command: ", msgObj_j);
}

function parse_chat_cmd(e) {
   var message = $('#chat-input').val().trim();
   var chat_msg = false;
   var msg_type = "invalid";

   // Determine if the message is a command, otherwise send it off as chat
   if (message) {
      if (message.charAt(0) == '/') {
         var args = message.split(' ');

         // remove the leading /
         if (args.length > 0 && args[0].startsWith('/')) {
            args[0] = args[0].slice(1);
         }
         var command = args[0];

         // Compare the lower-cased command
         switch(command.toLowerCase()) {
            // commands with no arguments
            case 'clear':
                console.log("Cleared scrollback");
               clear_chatbox();
               break;
            case 'reloadcss':
               console.log("Reloading CSS on user command");
               reload_css();
               chat_append('<div><span class="error">Reloaded CSS</span></div>');
               $('#chat-box').scrollTop($('#chat-box')[0].scrollHeight);
               break;
            case 'help':
               chat_append('<div><span class="error">*** HELP *** All commands start with /</span></div>');
               chat_append('<div><span class="error">/clear&nbsp;&nbsp;&nbsp;- Clear chat scrollback</span></div>');
               chat_append('<div><span class="error">/help&nbsp;&nbsp;&nbsp;&nbsp;- This help message</span></div>');
               chat_append('<div><span class="error">/me&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Show message as an ACTION in chat</span></div>');
               chat_append('<div><span class="error">/whois&nbsp;&nbsp;- Show user information: &lt;user&gt;</span></div>');
               //////
               chat_append('<br/><div><span class="error">*** DEBUG TOOLS *** All commands start with /</span></div>');
               chat_append('<div><span class="error">/reloadcss&nbsp;&nbsp;&nbsp;- Reload the CSS (stylesheet) without restarting the app.</span></div>');

               var isAdmin = /(owner|admin)/.test(auth_privs);
               if (isAdmin) {
                  chat_append('<br/><div><span class="error">*** ADMIN HELP *** These commands are only available to admins/owners./</span></div>');
                  chat_append('<div><span class="error">/ban&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Ban a user from logging in: &lt;user&gt; &lt;reason&gt;</span></div>');
                  chat_append('<div><span class="error">/die&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Shut down the server &lt;reason&gt;</span></div>');
//                  chat_append('<div><span class="error">/edit&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Edit a user: &lt;user&gt;</span></div>');
                  chat_append('<div><span class="error">/kick&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Kick a user: &lt;user&gt; &lt;reason&gt;</span></div>');
                  chat_append('<div><span class="error">/mute&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Mute a user, disables their TX and chat: &lt;user&gt; &lt;reason&gt;</span></div>');
                  chat_append('<div><span class="error">/restart&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Restart the server &lt;reason&gt;</span></div>');
                  chat_append('<div><span class="error">/unmute&nbsp;&nbsp;&nbsp;- Unmute a user, enables their TX (if privileged): &lt;user&gt;</span></div>');
               } else {
                  chat_append('<div><span class="error">*********************************************</span></div>');
                  chat_append('<div><span class="error">*** Additional commands are available to OWNER and ADMIN class users. ***</span></div>');
                  chat_append('<div><span class="error">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Contact the sysop to request more privileges, if needed.</span></div>');
               }
               break;
            case 'me':	// /me shows an ACTION in the chat
               message = message.slice(4);
               console.log("ACTION ", message);
               chat_msg = true;
               msg_type = "action";
               break;
            ////////////////////////////////////////////
            // All these are sent to the server as-is //
            //   It will respond if allowed or not    //
            ////////////////////////////////////////////
            case 'kick':
            case 'ban':
            case 'mute':
               if (args.length >= 2) {
                  args_obj = {
                     target: args[1],
                     reason: args.slice(2).join(' ') || ''
                  };
               }
               break;

            case 'whois':
            case 'edit':
               if (args.length >= 2) {
                  args_obj = {
                     target: args[1]
                  };
               }
               break;

            case 'die':
            case 'restart':
               args_obj = {
                  reason: args.slice(1).join(' ') || ''
               };
               break;
            default:
               chat_append('<div><span class="error">Invalid command:' + command + '</span></div>');
               break;
         }
         if (typeof args_obj === 'object' && args_obj !== null) {
            chat_send_command(command, args_obj);
         }
      } else {
        chat_msg = true;
        msg_type = "pub";
      }

      // Is this a user message that we should display?
      if (chat_msg) {
         var msgObj = {
            "talk": {
               "cmd": "msg",
               "ts": Math.floor(Date.now() / 1000),
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
}
