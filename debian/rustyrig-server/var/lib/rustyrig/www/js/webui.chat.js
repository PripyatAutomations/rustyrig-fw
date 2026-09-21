////////////////
// Chat Stuff //
////////////////
//
// Utility functions used stand-alone elsewhere
//
// Convert http(s) urls into clickable links
function msg_create_links(message) {
   return message.replace(
      /(https?:\/\/[^\s]+)/g,
      '<a href="$1" class="chat-link" target="_blank" rel="noopener">$1</a>'
   );
}

// Callsign responses contain data returned by an external lookup service.
// Escape it before inserting it into the chat DOM.
function webui_escape_html(value) {
   return String(value)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
}

/* PARITY: rustyrig-fw/rrclient/events.c:rrclient_handle_callsign */
function webui_parse_callsign_msg(msgObj) {
   const response = msgObj && msgObj.callsign;
   if (!response) {
      return;
   }

   const fields = response.fields && typeof response.fields === 'object' ? response.fields : null;
   if (!fields) {
      return;
   }

   const ts = msg_timestamp(msgObj.msg && msgObj.msg.ts);
   const rendered = [`<div class="chat-status notice">${ts}&nbsp;<b>CALLSIGN</b></div>`];
   if (fields) {
      if (response.status) {
         rendered.push(`<div class="chat-status">${ts}&nbsp;&nbsp;${webui_escape_html(response.status)}</div>`);
      }
      const preferred = [
         'callsign', 'name', 'email', 'address1', 'address2',
         'county', 'state', 'zip', 'country', 'wgs-84', 'heading',
         'license-effective', 'cached', 'cache-fetched', 'cache-expiry'
      ];
      const keys = Object.keys(fields).sort((a, b) => {
         const ai = preferred.indexOf(a.replace(/_/g, '-'));
         const bi = preferred.indexOf(b.replace(/_/g, '-'));
         if (ai >= 0 || bi >= 0) return (ai < 0 ? preferred.length : ai) - (bi < 0 ? preferred.length : bi);
         return a.localeCompare(b);
      });
      keys.forEach(key => {
         const label = key.replace(/_/g, '-');
         rendered.push(`<div class="chat-status">${ts}&nbsp;&nbsp;<span class="chat-msg-prefix">${webui_escape_html(label)}:</span>${webui_escape_html(fields[key])}</div>`);
      });
   }
   ChatBox.Append(rendered.join(''));
}

// Server sends booleans sometimes as real JSON bools (dict_add_bool) and
// sometimes as "true"/"false" strings (dict_add); handle both
function parse_bool_field(val) {
   return val === true || val === 'true';
}

class WebUiChat {
   constructor(output, input) {
      if (typeof output === 'undefined' || output === null) {
         return null;
      }

      this.output = $(output);
      this.scrollback_max_lines = 1000;
      this.scrollback_purge_lines = 100;
   }

   // Add a message to the chat
   Append(msg) {
      // limit scrollback to 1000 items
      const $messages = $('#chat-box').children();

      // Limit scrollback size
      if ($messages.length > this.scrollback_max_lines) {
         $messages.slice(0, this.scrollback_purge_lines).remove();
      }

      // Add the message to the chatbox
      this.output.append(msg);

      // A few ms delay before scrolling to improve smoothness
      setTimeout(function () {
         $('#chat-box').scrollTop($('#chat-box')[0].scrollHeight);
      }, 10);
   }

   Clear() {
      this.output.empty();

      setTimeout(function () {
         $('#chat-box').scrollTop($('#chat-box')[0].scrollHeight);
      }, 10);
   }
}

if (!window.webui_inits) {
   window.webui_inits = [];
}
window.webui_inits.push(function webui_chat_init() { chat_init(); });

function chatbox_clear() {
   $('#chat-box').empty();
}

function chat_init() {
  $(document).ready(function() {
      let chatBox = $('#chat-box');

      // scroll the chatbox down when window is resized (keyboard open/closed, etc)
      $(window).on('resize', function() {
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
   UserCache.clear();

   $('.cul-list').empty();
   $('.cul-list').append('<span class="error">OFFLINE</span>');
}

// Store the data from names reply in the UserCache, replacing outdated informations
function parse_userinfo_reply(message) {
//    console.log("parse_userinfo_reply:", message);
    if (typeof message !== 'undefined') {
       // Server sends the PTT state as talk.tx (see srv.chat.c: ws_send_userinfo)
       UserCache.update({ name: message.talk.user, privs: message.talk.privs, muted: parse_bool_field(message.talk.muted), ptt: parse_bool_field(message.talk.tx !== undefined ? message.talk.tx : message.talk.ptt), sessions: message.talk.sessions });
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
       } else if (parse_bool_field(user.muted)) {
           tx_badges += '<span class="badge">🙊</span>';
        }

        if (privs.has('owner')) {
           badges += '<span class="badge owner-badge">👑&nbsp;</span>';
       } else if (privs.has('admin')) {
           badges += '<span class="badge admin-badge">⭐&nbsp;</span>';
       } else if (privs.has('noob')) {
           badges += '<span class="badge admin-badge">🐣&nbsp;</span>';
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
       if (parse_bool_field(user.muted)) {
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
   var msgObj = {
      "msg": {
         "type": "talk"
      },
      "talk": {
         "cmd": cmd,
         "token": auth_token
      }
   };

   // args: target goes into talk.target (server reads talk.target),
   // everything else into talk.args.<key> (e.g. talk.args.reason)
   if (typeof args === 'object' && args !== null) {
      if (typeof args.target !== 'undefined') {
         msgObj.talk.target = args.target;
      }
      if (typeof args.data !== 'undefined') {
         msgObj.talk.data = args.data;
      }
      var rest = { ...args };
      delete rest.target;
      delete rest.data;
      if (Object.keys(rest).length > 0) {
         msgObj.talk.args = rest;
      }
   }

   var msgObj_j = JSON.stringify(msgObj);
   socket.send(msgObj_j);
   $('#user-menu').hide();
//   console.log("Sent command:", msgObj_j);
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
               ChatBox.Append('<div><span class="error">Cleared syslog window</span></div>');
               syslog_clear();
               break;
            case 'clxfr':
               ChatBox.Append('<div><span class="error">Cleared xfer-chunks</span></div>');
               clear_xfer_chunks();
               break;
            case 'chat':
               wmSwitchTab('chat');
               break;
            case 'cfg':
            case 'config':
               wmSwitchTab('cfg');
               break;
            case 'rig':
               wmSwitchTab('rig');
               break;
            case 'log':
               wmSwitchTab('syslog');
               break;
            case 'menu':
               show_user_menu(args[1]);
               break;
            case 'logout':
            case 'quit':
               logout();
               break;
            case 'reloadcss':
               console.log("Reloading CSS on user command");
               reload_css();
               ChatBox.Append('<div><span class="notice">Reloaded CSS</span></div>');
               break;
            case 'rxvol':
               $('#rig-rx-vol').val(args[1] / 100);
               console.log("Set volume to " + args[1] + "%");
               rxGainNode.gain.value = parseFloat($('#rig-rx-vol').val());
               break;
            case 'rxcodec':
            case 'txcodec': {
               var codec_direction_tx = command.toLowerCase() === 'txcodec';
               var requested_codec = args.length > 1 ? args[1].toLowerCase() : 'list';
               if (requested_codec === 'list') {
                  ChatBox.Append('<div><span class="notice">' +
                     (codec_direction_tx ? 'TX' : 'RX') + ' codecs: ' +
                     'NONE ' + webui_audio_codec_list().join(' ') + '</span></div>');
               } else if (!webui_audio_set_codec(requested_codec, codec_direction_tx,
                  args.length > 2 ? args[2] : null)) {
                  ChatBox.Append('<div><span class="error">Unsupported browser audio codec: ' +
                     requested_codec + '</span></div>');
               }
               break;
            }
            case 'qrz':
            case 'grid': {
               const lookup = args.slice(1).join(' ').trim();
               if (!lookup) {
                  ChatBox.Append('<div><span class="error">Usage: /' + command.toLowerCase() +
                     (command.toLowerCase() === 'grid' ? ' GRID|LAT,LON' : ' CALLSIGN') + '</span></div>');
               } else {
                  args_obj = { data: lookup };
               }
               break;
            }
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
            case 'media':
               // PARITY: rustyrig-fw/rrclient/media.c: cmd_media()
               var sub = (args.length > 1 ? args[1].toLowerCase() : 'list');

               if (sub === 'list' || sub === '') {
                  mediaListChannels();
                  if (args.length < 2) {
                     requestMediaChannels();
                  }
               } else if (sub === 'sub' || sub === 'subscribe' ||
                          sub === 'unsub' || sub === 'unsubscribe') {
                  if (args.length < 3 || args[2] === '') {
                     ChatBox.Append('<div><span class="error">Usage: /media ' + sub + ' &lt;uuid|#&gt;</span></div>');
                     break;
                  }
                  var chan = mediaChanLookup(args[2]);
                  var unsub = (sub.startsWith('un'));

                  if (!chan) {
                     if (unsub) {
                        ChatBox.Append('<div><span class="error">No such channel |' + args[2] + '|</span></div>');
                     } else {
                        // Not known locally: pass through, server may create one
                        subscribeMediaChannel(args[2]);
                     }
                     break;
                  }
                  if (unsub) {
                     if (!chan.subscribed) {
                        ChatBox.Append('<div><span class="notice">Not subscribed to ' + chan.uuid + '</span></div>');
                     } else {
                        ChatBox.Append('<div><span class="notice">Unsubscribing from ' + chan.uuid + '</span></div>');
                        unsubscribeMediaChannel(chan.uuid);
                     }
                  } else {
                     if (chan.subscribed) {
                        ChatBox.Append('<div><span class="notice">Already subscribed to ' + chan.uuid + '</span></div>');
                     } else {
                        ChatBox.Append('<div><span class="notice">Subscribing to ' + chan.uuid + '</span></div>');
                        subscribeMediaChannel(chan.uuid);
                     }
                  }
               } else {
                  ChatBox.Append('<div><span class="error">Usage: /media [LIST | SUB|SUBSCRIBE &lt;uuid|#&gt; | UNSUB|UNSUBSCRIBE &lt;uuid|#&gt;]</span></div>');
               }
               break;
            case 'help':
               ChatBox.Append('<div><span class="notice">*** HELP *** All commands start with /</span></div>');
               ChatBox.Append('<div><span class="notice">/ chat | (cfg|config) | rig | log to switch tabs</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/quit&nbsp;&nbsp;-&nbsp;End session</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/clear&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Clear chat scrollback</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/help&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- This help message</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/me&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Show message as an ACTION in chat</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/menu&nbsp;&nbsp;&nbsp;&nbsp;- Show the user menu &lt;user&gt;</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/whois&nbsp;&nbsp;&nbsp;- Show user information: &lt;user&gt;</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/qrz&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Look up a callsign: &lt;callsign&gt; [NOCACHE]</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/grid&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Look up a grid square or coordinates: &lt;grid|lat,lon&gt;</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/quit&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Disconnect</span></div>');

               ChatBox.Append('<br/><div><span class="notice">*** AUDIO - Audio Settings</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/media&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Media channels: LIST | SUBSCRIBE &lt;uuid|#&gt; | UNSUBSCRIBE &lt;uuid|#&gt;</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/rxcodec [codec|NONE|LIST] [uuid|#number] | /txcodec [codec|NONE|LIST] [uuid|#number] - Select browser audio codec</span></div>');
            ChatBox.Append('<div><span class="notice">&nbsp;/rxvol&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Set volume in % [vol]</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/rxmute | /rxunmute&nbsp;&nbsp;&nbsp;- Mute/Unmute RX audio</span></div>');

               //////
               ChatBox.Append('<br/><div><span class="notice">*** DEBUG TOOLS - Used by developer</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/clearlog&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Clear the syslog window</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/clxfr&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Clear file xfer cache</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/names&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Force refresh of UserCache</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/reloadcss&nbsp;&nbsp;- Reload the CSS (stylesheet) without restarting the app.</span></div>');
               ChatBox.Append('<div><span class="notice">&nbsp;/syslog&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Toggle syslog traffic [on|off].</span></div>');

               var isAdmin = /(owner|admin)/.test(auth_privs);
               if (isAdmin) {
                  ChatBox.Append('<br/><div><span class="notice">*** ADMIN HELP *** These commands are only available to admins/owners.</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;/ban&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Ban a user from logging in: &lt;user&gt; &lt;reason&gt;</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;/die&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Shut down the server &lt;reason&gt;</span></div>');
                  ChatBox.Append('<div><span class="notice">/edit&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Edit a user: &lt;user&gt;</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;/kick&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Kick a user: &lt;user&gt; &lt;reason&gt;</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;/mute&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Mute a user, disables their TX and chat: &lt;user&gt; &lt;reason&gt;</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;/quota&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- TX quota admin (bare /quota = list+help): LIST | SHOW &lt;user&gt;... | ADD &lt;user&gt; &lt;mins&gt; | RESET &lt;user&gt;... | SET &lt;user&gt; &lt;mins&gt;</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;/rehash&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Reload the server configuration &amp; user database</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;/restart&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Restart the server &lt;reason&gt;</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;/unmute&nbsp;&nbsp;&nbsp;- Unmute a user, enables their TX (if privileged): &lt;user&gt;</span></div>');
               } else {
                  ChatBox.Append('<div><span class="notice">*********************************************</span></div>');
                  ChatBox.Append('<div><span class="notice">*** Additional commands are available to OWNER and ADMIN class users. ***</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Contact the sysop to request more privileges, if needed.</span></div>');
               }
               ChatBox.Append('<br/><div><span class="notice">You can use tab completion, press @ then type a few letters or hit tab</span></div>');
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

            case 'quota':
               // PARITY: rustyrig-fw/rrclient/cmd.admin.c: cmd_quota()
               // Server checks admin/owner privs; sends the raw tail as data
               // plus a single-user target if one arg was given
               if (args.length >= 2) {
                  args_obj = {
                     target: args[1]
                  };
               } else {
                  // Bare /quota is a shortcut for LIST + showing the help
                  args_obj = {
                     target: 'LIST'
                  };
                  ChatBox.Append('<div><span class="notice">Usage: /quota LIST | SHOW &lt;user&gt;... | ADD &lt;user&gt; &lt;minutes&gt; | RESET &lt;user&gt;... | SET &lt;user&gt; &lt;minutes&gt;</span></div>');
                  ChatBox.Append('<div><span class="notice">&nbsp;&nbsp;ADD/SET take minutes (0 = no TX allowed); SHOW shows seconds too.</span></div>');
               }
               break;

            case 'rehash':
               // PARITY: rustyrig-fw/rrclient/cmd.admin.c: cmd_rehash()
               // Server checks admin/owner privs; reloads cfg + user db
               socket.send(JSON.stringify({
                  "msg": { "type": "rehash" },
                  "ts": Math.floor(Date.now() / 1000)
               }));
               ChatBox.Append('<div><span class="notice">Rehash requested from server</span></div>');
               break;
            case 'die':
            case 'restart':
               args_obj = {
                  reason: args.slice(1).join(' ') || ''
               };
               break;
            default:
               ChatBox.Append('<div><span class="error">Invalid command: ' + command + '</span></div>');
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
            "msg": {
               "type": "talk"
            },
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

      // Record the sent line in the input history (for up/down recall)
      /* PARITY: rustyrig-fw/librustyaxe/tui.keys.c history_add() */
      chat_history_add(message);

      // Clear the input field and after a delay re-focus it, to avoid flashing
      $('#chat-input').val('');
      setTimeout(function () {
           $('#chat-input').focus();
      }, 10);
   }
}

const UserCache = {
   users: {},

   add(user) {
      // I dont remember why this is done as such; ideally we should duplicate user object into this.users[user.name] directly
      this.users[user.name] = {
         ...(user.hasOwnProperty('ptt')   && { ptt:   user.ptt }),
         ...(user.hasOwnProperty('muted') && { muted: user.muted }),
         ...(user.hasOwnProperty('privs') && { privs: user.privs }),
         ...(user.hasOwnProperty('sessions') && { sessions: user.sessions })
      };
      console.log("UC.add: name:", user.name, "sessions:", user.sessions);
      cul_render();
      if (typeof ptt_button_apply === 'function') ptt_button_apply();
   },

   remove(name) {
      const entry = this.users[name];
      if (!entry) return;

      console.log("UC.remove: name:", name, "sessions:", entry.sessions);
      if (entry.sessions <= 1) {
         delete this.users[name];
      } else {
         entry.sessions--;
      }

      cul_render();
      if (typeof ptt_button_apply === 'function') ptt_button_apply();
   },

   update(user) {
      const existing = this.users[user.name];

      if (!existing) {
         console.log("UC.update: No entry for", user.name, "- creating new");
         this.add(user);
         return;
      }
      if ('ptt'   in user) existing.ptt   = user.ptt;
      if ('privs' in user) existing.privs = user.privs;
      if ('muted' in user) existing.muted = user.muted;
      if ('sessions' in user) existing.sessions = user.sessions;
      cul_render();
   },

   get(name) {
      return this.users[name] || null;
   },

   get_all() {
      return Object.entries(this.users).map(([name, props]) => ({ name, ...props }));
   },

   sessions(name) {
      return this.users[name]?.sessions || 0;
   },

   dump() {
      console.log("UserCache contents:");
      for (const [name, props] of Object.entries(this.users)) {
         console.log(`- ${name}:`, props);
      }
   },

   clear() {
      this.users = {};
      cul_render();
   }
};

function user_link(username) {
   if (username === auth_user) {
      return `<a href="#" class="my-link" onclick="show_user_menu('${username.replace(/'/g, "\\'")}'); return false;">${username}</a>`;
   } else {
      return `<a href="#" class="other-link" onclick="show_user_menu('${username.replace(/'/g, "\\'")}'); return false;">${username}</a>`;
   }
}

/*
      if (typeof input === 'undefined' || input === null) {
         return null;
      }
      this.input = $(input);
*/
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
//      const Inputbox = new WebUiInput('#chat-input');


/////////////////////////////////
/////////////////////////////////
/////////////////////////////////
function webui_parse_chat_msg(msgObj) {
   var cmd = msgObj.talk.cmd;
   var message = msgObj.talk.data;

   // keep msg up top as it's the most frequently encountered command
   // XXX: Maybe we should keep a counter of received commands so we can optimize this a bit later??
   var msg_ts = msg_timestamp(msgObj.msg.ts);

   if (cmd === 'replay-start') {
      ChatBox.Append('<div>' + msg_ts + ' *** Chat replay Start ***</div>');
   } else if (cmd === 'replay-complete' || cmd === 'replay-completed') {
      ChatBox.Append('<div>' + msg_ts + ' *** Chat replay End ***</div>');
   } else if (cmd === 'msg' && message) {
      var sender = msgObj.talk.from;
      var msg_type = msgObj.talk.msg_type;

      if (msg_type === "file_chunk") {
         handle_file_chunk(msgObj);
      } else if (msg_type === "action" || msg_type == "pub") {
         message = msg_create_links(message);
         // Don't play a bell or set highlight on SelfMsgs
         if (sender === auth_user) {
            if (msg_type === 'action') {
               ChatBox.Append('<div>' + msg_ts + ' <span class="chat-my-msg-prefix">&nbsp;==>&nbsp;</span>***&nbsp;' + sender + '&nbsp;***&nbsp;<span class="chat-my-msg">' + message + '</span></div>');
            } else if (msg_type === 'pub') {
               ChatBox.Append('<div>' + msg_ts + ' <span class="chat-my-msg-prefix">&nbsp;==>&nbsp;</span><span class="chat-my-msg">' + message + '</span></div>');
            }
         } else {
            if (msg_type === 'action') {
               ChatBox.Append('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">&nbsp;' + sender + '&nbsp;</span>***&nbsp;<span class="chat-msg">' + message + '</span></div>');
            } else if (msg_type === 'pub') {
               ChatBox.Append('<div>' + msg_ts + ' <span class="chat-msg-prefix">&lt;' + sender + '&gt;&nbsp;</span><span class="chat-msg">' + message + '</span></div>');
            }

            play_notify_bell();
            set_highlight("chat");
            // XXX: Update the window title to show a pending message
         }
      } else if (msg_type === "replay-action" || msg_type == "replay-pub" || msg_type == 'replay-privmsg' || msg_type == 'replay-priv') {
         message = msg_create_links(message);

         if (msg_type === 'replay-action') {
            ChatBox.Append('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">&nbsp;' + sender + '&nbsp;</span>***&nbsp;<span class="chat-msg">' + message + '</span></div>');
         } else if (msg_type === 'replay-pub') {
            ChatBox.Append('<div>' + msg_ts + ' <span class="chat-msg-prefix">&lt;' + sender + '&gt;&nbsp;</span><span class="chat-msg">' + message + '</span></div>');
         } else {  // replay-privmsg / replay-priv
            ChatBox.Append('<div>' + msg_ts + ' <span class="chat-msg-prefix">*' + sender + '*&nbsp;</span><span class="chat-msg">' + message + '</span></div>');
         }
         set_highlight("chat");
         // XXX: Update the window title to show a pending message
      }
   } else if (cmd === 'join') {
      var user = msgObj.talk.user;
      var privs = msgObj.talk.privs;

      if (typeof user !== 'undefined') {
         var msg_ts = msg_timestamp(msgObj.msg.ts);
         var nl = user_link(user);
         var ptt_state = msgObj.talk.ptt;

         if (typeof ptt_state === 'undefined') {
            ptt_state = false;
         }

         var muted_state = parse_bool_field(msgObj.talk.muted);

         var sessions = msgObj.talk.sessions;
         if (typeof sessions !== 'undefined') {
            UserCache.add({ name: user, ptt: ptt_state, muted: muted_state, privs: privs, sessions: sessions });
         } else {
            UserCache.add({ name: user, ptt: ptt_state, muted: muted_state, privs: privs });
         }

         ChatBox.Append('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + nl + '&nbsp;</span><span class="chat-msg">connected to the radio</span>&nbsp;***</div>');
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
      var user = msgObj.talk.user;
      // Play leave (door close) sound if the bell button is checked
      if ($('#bell-btn').data('checked') && user && user !== auth_user) {
         leave_ding.currentTime = 0;  // Reset audio to start from the beginning
         leave_ding.play();
      }
      if (user) {
         UserCache.remove(user);
      }
      console.log("Kick command received for user:", user, " reason:", msgObj.talk.data);
   } else if (cmd === 'mute') {
      var user = msgObj.talk.user;
      // Shows a muted icon
      console.log("Mute command received for user:", user);
      UserCache.update({ name: user, muted: true });

      // this is for us, so disable the PTT button
      if (user === auth_user) {
         $('button.rig-ptt').attr("disabled", "disabled");
      }
   } else if (cmd === "quit") {
      var user = msgObj.talk.user;
      var reason = msgObj.talk.reason;

      if (user) {
         var msg_ts = msg_timestamp(msgObj.msg.ts);
         if (typeof reason === 'undefined') {
            reason = 'Client exited';
         }

         UserCache.remove(user);
         // Play leave (door close) sound if the bell button is checked
         if ($('#bell-btn').data('checked')) {
            if (user !== auth_user) {
               leave_ding.currentTime = 0;  // Reset audio to start from the beginning
               leave_ding.play();
            }
         }

         ChatBox.Append('<div>' + msg_ts + ' ***&nbsp;<span class="chat-msg-prefix">' + user + '&nbsp;</span><span class="chat-msg">disconnected: ' + reason + '</span>&nbsp;***</div>');
      } else {
         console.log("got %s for undefined user, ignoring", cmd);
      }
   } else if (cmd === "userinfo") {
      parse_userinfo_reply(msgObj);
   } else if (cmd === "unmute") {
      var user = msgObj.talk.user;
      UserCache.update({ name: user, muted: false });

      // this is for us, so re-enable the PTT button, if appropriate
      if (user === auth_user) {
         $('button.rig-ptt').removeAttr("disabled");
      }
   } else if (cmd === 'whois') {
      // Flat whois reply (see srv.chat.c): talk.username/email/privs/muted/sessions
      // talk.connected/last_heard (unix ts) and talk.ua
      // Rendered IRC-style in the chat scrollback
      const username = msgObj.talk.username;

      if (!username) {
         return;
      }

      const who_ts = msg_timestamp(msgObj.msg.ts);
      const who_line = (text, cls) => {
         ChatBox.Append(`<div>${who_ts}&nbsp;<span class="chat-msg-prefix">***&nbsp;</span><span class="${cls || 'chat-msg'}">${text}</span></div>`);
      };

      who_line(`Whois for <b>${username}</b>`, 'notice');
      who_line(`Email:&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;${msgObj.talk.email || 'none'}`);
      who_line(`Privileges:&nbsp;&nbsp;${msgObj.talk.privs || 'None'}`);
      if (parse_bool_field(msgObj.talk.muted)) {
         who_line(`This user is currently MUTEd. Rigctl is temporarily suspended.`, 'error');
      }
      who_line(`Sessions:&nbsp;&nbsp;&nbsp;&nbsp;${msgObj.talk.sessions || 0}`);
      if (msgObj.talk.connected) {
         who_line(`Connected:&nbsp;&nbsp;&nbsp;${new Date(msgObj.talk.connected * 1000).toLocaleString()}`);
      }
      if (msgObj.talk.last_heard) {
         who_line(`Last heard:&nbsp;&nbsp;${new Date(msgObj.talk.last_heard * 1000).toLocaleString()}`);
      }
      who_line(`Client:&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;${msgObj.talk.ua || 'unknown'}`);
      who_line(`End of WHOIS ${username}`, 'notice');
   } else {
      console.log("Unknown talk command:", cmd, "msg:", msgData);
   }
}
