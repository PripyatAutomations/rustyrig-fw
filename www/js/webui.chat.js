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
   // limit scrollback to 1000 items
//   const $messages = $('#chat-box');
   const $messages = $('#chat-box').children();
   // If we have over 1000 lines, remove 100
   if ($messages.length > 1000) {
      $messages.slice(0, 100).remove();
   }

   $('#chat-box').append(msg);
   setTimeout(function () {
      $('#chat-box').scrollTop($('#chat-box')[0].scrollHeight);
   }, 10);
};

function chatbox_clear() {
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
         console.log("PASTE: Unsupported file type:", item.type);
         break;
      }
   }
}

if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_chat_init() { chat_init(); });

function chat_init() {
  $(document).ready(function() {
      let chatBox = $('#chat-box');

      // scroll the chatbox down when window is resized (keyboard open/closed, etc)
      $(window).on('resize', function() {
/* XXX: Or maybe we want to only scroll if they're near the bottom already? Need to figure out common kbd size and replace the 50 with the max
         let el = chatBox[0];
         if (el.scrollHeight - el.scrollTop - el.clientHeight < 50) {
            chatBox.scrollTop(el.scrollHeight);
         }
 */
         chatBox.scrollTop(chatBox[0].scrollHeight);
      });

      $('#chat-input').on('paste', function(e) {
         handle_paste(e);
         e.preventDefault();
      });

      $('#send-btn').click(function(e) {
         parse_chat_cmd(e);
      });

      // Ensure #chat-box does not accidentally become focusable
      $('#chat-box').attr('tabindex', '-1');
      $('.um-close').click(function() {
         form_disable(false);
         $('#user-menu').hide('slow');
      });

      function applyChatFontSize(size) {
         $("#chatbox").css("font-size", size + "px");
         localStorage.setItem("chatFontSize", size);
      }

      $(function() {
         const savedSize = localStorage.getItem("chatFontSize") || 16;
         $("#ui-chat-font").val(savedSize);
         applyChatFontSize(savedSize);

         $("#ui-chat-font").on("input", function() {
            applyChatFontSize(this.value);
         });
      });

   });
}

function cul_offline() {
   // Clear the user-info cache (populated from JOIN messages)
   user_cache = {};

   $('.cul-list').empty();
   $('.cul-list').append('<span class="error">OFFLINE</span>');
}

// Store the data from names reply in the UserCache, replacing outdated informations
function parse_userinfo_reply(message) {
//    console.log("parse_userinfo_reply:", message);
    if (typeof message !== 'undefined') {
       UserCache.update({ name: message.talk.user, privs: message.talk.privs, muted: message.talk.muted, ptt: message.talk.ptt, clones: message.talk.clones });
    }

    return false;
}

// Re-render the #chat-user-list from UserCache contents
function cul_render() {
    $('.cul-list').empty();
    const users = UserCache.get_all();

    // Only show each user once in the list
    const uniqueUsers = Array.from(
       new Map(users.map(u => [u.name.toLowerCase(), u])).values()
    );

    uniqueUsers.sort((a, b) => a.name.localeCompare(b.name, undefined, {
       sensitivity: 'base',
       numeric: true
    }));

    uniqueUsers.forEach(user => {
//       console.log("cul_render:", user.name, "data:", user);
       const { name } = user;
       const privs = new Set((user.privs || '').split(',').map(p => p.trim()));
       let badges = '', tx_badges = '';

       if (privs.has('elmer')) {
           tx_badges +=  '<span class="badge admin-badge">🧙&nbsp;</span>';
       }
       if (user.ptt) {
          tx_badges += '<span class="badge tx-badge">🎙️</span>';
       } else if (user.muted === "true") {
           tx_badges += '<span class="badge">🙊</span>';
       }

       if (privs.has('owner')) {
           badges += '<span class="badge owner-badge">👑&nbsp;</span>';
       } else if (privs.has('admin')) {
           badges += '<span class="badge admin-badge">⭐&nbsp;</span>';
       } else if (privs.has('noob')) {
           badges += '<span class="badge admin-badge">🐣&nbsp;</span>';
       } else if ((user.name || '').toUpperCase() === 'N9MSC') {
           badges += '<span class="badge admin-badge">🐒&nbsp;</span>';
       } else if (privs.has('tx')) {
           badges +=  '<span class="badge admin-badge">👤&nbsp;</span>';
       } else {
           badges += '<span class="badge view-badge">👀&nbsp;</span>';
//           badges += '<span class="badge empty-badge">&nbsp;✴&nbsp;</span>';
       }

       // Change color for our own name in CUL
       let cul_class = 'cul-other';
       if (auth_user === user.name) {
          cul_class = 'cul-self';
        }

       // render the user item (li)
       const userItem = `<li>
          <span class="chat-user-list" onclick="show_user_menu('${user.name}')">
             ${badges}<span class="${cul_class}">${user.name}</span>${tx_badges}
          </span>
       </li>`;

       $('.cul-list').append(userItem);
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

   title.textContent = `Enter ${cmd} reason for ${username}`;
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
        <hr width="50%"/>Admin<br/><br/>
        <li>
         <button class="cul-menu-button mute-user" title="Mute (disable CAT/TX) for user">Mute</button>
         <button class="cul-menu-button unmute-user" title="Unmute (enable CAT/TX) for user">Unmute</button>
        </li>
        <li><button class="cul-menu-button kick-user" title="Disconnect user">Kick</button></li>
        <li><button class="cul-menu-button ban-user" title="Ban & Kick user">Ban</button></li>
    `;

    // Base menu
    var user_email = 'none';

    var menu = `
        <div class="um-header" style="position: relative;">
            <span class="um-close">✖</span>
        </div><br/>
        <center>User: ${username}</center><br/>
        <span class="user-menu-items">
            <ul>
<!--                <li><a href="mailto:${user_email}" target="_blank">Email</a></li> -->
                <li><button class="cul-menu-button whois-user">Whois</button></li>
                ${isAdmin ? admin_menu : ''}
            </ul>
        </span>
    `;

    // Update the user menu and show it
    $('#user-menu').html(menu);

    var user = UserCache.get(username);
    // if user is in the cache, see if they have muted property set
    if (typeof user !== 'undefined') {
       $('.mute-user').on('click', function() {
          chat_send_command('mute', { target: username });
          form_disable(false);
          $('#user-menu').hide('slow');
       });

       $('.unmute-user').on('click', function() {
          chat_send_command('unmute', { target: username });
          form_disable(false);
          $('#user-menu').hide('slow');
       });

       // do we show mute or unmute button?
       if (user.muted === "true") {
          $('.mute-user').hide('fast');
          $('.unmute-user').show('fast');
       } else {
          $('.unmute-user').hide('fast');
          $('.mute-user').show('fast');
       }
    }

    // Close button functionality
    $('.um-close').on('click', function() {
        form_disable(false);
        $('#user-menu').hide('slow');
    });

    // Attach event listeners
    $('.whois-user').on('click', () => chat_send_command('whois', { target: username }));
    $('.kick-user').on('click', () => send_admin_command('kick', username));
    $('.ban-user').on('click', () => send_admin_command('ban', username));

    // Finally, show the menu
    $('#user-menu').show();
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
//   console.log("Sent command:", msgObj_j);
}

function show_chat_window() {
   active_tab = 'chat';
//   $('.chroma-hash').hide();
   $('div#win-login').hide();
   $('div#win-rig').hide();
   $('div#win-config').hide();
   $('div#win-syslog').hide();
   $('div#tabstrip').show();
   $('div#win-chat').show();
   $('#chat-input').focus();
}

var unmute_vol = $('#rig-rx-vol').val();

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
               chatbox_clear();
               break;
            case 'clearlog':
               chat_append('<div><span class="error">Cleared syslog window</span></div>');
               syslog_clear();
               break;
            case 'clxfr':
               chat_append('<div><span class="error">Cleared xfer-chunks</span></div>');
               clear_xfer_chunks();
               break;
            case 'chat':
               show_chat_window();
               break;
            case 'cfg':
            case 'config':
               show_config_window();
               break;
            case 'rig':
               show_rig_window();
               break;
            case 'log':
               show_syslog_window();
               break;
            case 'menu':
               show_user_menu(args[1]);
               break;
            case 'logout':
            case 'quit':
               logout();
            case 'reloadcss':
               console.log("Reloading CSS on user command");
               reload_css();
               chat_append('<div><span class="notice">Reloaded CSS</span></div>');
               break;
            case 'rxvol':
               $('#rig-rx-vol').val(args[1] / 100);
               console.log("Set volume to " + args[1] + "%");
               rxGainNode.gain.value = parseFloat($('#rig-rx-vol').val());
               break;
            case 'rxmute':
               unmute_vol = $('#rig-rx-vol').val();
               rxGainNode.gain.value = 0;
               console.log("Muting RX audio");
               break;
            case 'rxunmute':
               $('#rig-rx-vol').val(unmute_vol);
               console.log("Unmuting RX audio");
               rxGainNode.gain.value = unmute_vol;
               break;
            case 'help':
               chat_append('<div><span class="notice">*** HELP *** All commands start with /</span></div>');
               chat_append('<div><span class="notice">/ chat | (cfg|config) | rig | log to switch tabs</span></div>');
               chat_append('<div><span class="notice">&nbsp;/quit&nbsp;&nbsp;-&nbsp;End session</span></div>');
               chat_append('<div><span class="notice">&nbsp;/clear&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Clear chat scrollback</span></div>');
               chat_append('<div><span class="notice">&nbsp;/help&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- This help message</span></div>');
               chat_append('<div><span class="notice">&nbsp;/me&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Show message as an ACTION in chat</span></div>');
               chat_append('<div><span class="notice">&nbsp;/menu&nbsp;&nbsp;&nbsp;&nbsp;- Show the user menu &lt;user&gt;</span></div>');
               chat_append('<div><span class="notice">&nbsp;/whois&nbsp;&nbsp;&nbsp;- Show user information: &lt;user&gt;</span></div>');
               chat_append('<div><span class="notice">&nbsp;/quit&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Disconnect</span></div>');

               chat_append('<br/><div><span class="notice">*** AUDIO - Audio Settings</span></div>');
               chat_append('<div><span class="notice">&nbsp;/rxvol&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Set volume in % [vol]</span></div>');
               chat_append('<div><span class="notice">&nbsp;/rxmute | /rxunmute&nbsp;&nbsp;&nbsp;- Mute/Unmute RX audio</span></div>');

               //////
               chat_append('<br/><div><span class="notice">*** DEBUG TOOLS - Used by developer</span></div>');
               chat_append('<div><span class="notice">&nbsp;/clearlog&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Clear the syslog window</span></div>');
               chat_append('<div><span class="notice">&nbsp;/clxfr&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Clear file xfer cache</span></div>');
               chat_append('<div><span class="notice">&nbsp;/names&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Force refresh of UserCache</span></div>');
               chat_append('<div><span class="notice">&nbsp;/reloadcss&nbsp;&nbsp;- Reload the CSS (stylesheet) without restarting the app.</span></div>');
               chat_append('<div><span class="notice">&nbsp;/syslog&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Toggle syslog traffic [on|off].</span></div>');

               var isAdmin = /(owner|admin)/.test(auth_privs);
               if (isAdmin) {
                  chat_append('<br/><div><span class="notice">*** ADMIN HELP *** These commands are only available to admins/owners.</span></div>');
                  chat_append('<div><span class="notice">&nbsp;/ban&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Ban a user from logging in: &lt;user&gt; &lt;reason&gt;</span></div>');
                  chat_append('<div><span class="notice">&nbsp;/die&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Shut down the server &lt;reason&gt;</span></div>');
//                  chat_append('<div><span class="notice">/edit&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Edit a user: &lt;user&gt;</span></div>');
                  chat_append('<div><span class="notice">&nbsp;/kick&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Kick a user: &lt;user&gt; &lt;reason&gt;</span></div>');
                  chat_append('<div><span class="notice">&nbsp;/mute&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Mute a user, disables their TX and chat: &lt;user&gt; &lt;reason&gt;</span></div>');
                  chat_append('<div><span class="notice">&nbsp;/restart&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Restart the server &lt;reason&gt;</span></div>');
                  chat_append('<div><span class="notice">&nbsp;/unmute&nbsp;&nbsp;&nbsp;- Unmute a user, enables their TX (if privileged): &lt;user&gt;</span></div>');
               } else {
                  chat_append('<div><span class="notice">*********************************************</span></div>');
                  chat_append('<div><span class="notice">*** Additional commands are available to OWNER and ADMIN class users. ***</span></div>');
                  chat_append('<div><span class="notice">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Contact the sysop to request more privileges, if needed.</span></div>');
               }
               chat_append('<br/><div><span class="notice">You can use tab completion, press @ then type a few letters or hit tab</span></div>');
               break;
            case 'me':	// /me shows an ACTION in the chat
               message = message.slice(4);
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
            case 'names':
               if (args.length >= 2) {
                  args_obj = {
                     target: args[1],
                     reason: args.slice(2).join(' ') || ''
                  };
               } else {
                  args_obj = {
                     target: args[1],
                  };
               }
               break;

            case 'edit':
            case 'syslog':
            case 'unmute':
            case 'whois':
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
