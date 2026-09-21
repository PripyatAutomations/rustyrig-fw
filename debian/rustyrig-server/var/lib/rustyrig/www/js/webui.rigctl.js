/*
 * rig control (CAT over websocket)
 */
var active_vfo = 'A';		// Active VFO
var ptt_active = false;
var ptt_by_vfo = {};
var ptt_pending = false;
var ptt_pending_state = false;
var ptt_pending_timer = null;
// How do we fill this from the radio.config.json?? that would solve a lot of problems
const FREQ_DIGITS = 8;		// How many digits of frequency to display - 10 digits = single ghz
var rig_modes = [ 'LSB', 'USB', 'AM', 'FM', 'D-L', 'D-U' ];

if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_rigctl_init() {
   let status = "OK";

   console.log("[rigctl] init: start");

   if (vfo_edit_init()) {
      status = "ERR";
   }
   if (ptt_btn_init()) {
      status = "ERR";
   }
   if (freq_input_init()) {
      status = "ERR";
   }

   console.log("[rigctl] init: end. status=" + status);
});

function send_cat_msg() {
}

function vfo_edit_init() {
   $('span#vfo-a-freq').click(function(e) {
      $('#edit-vfo-freq').toggle(300);
   });

   $('span#vfo-a-mode').click(function(e) {
      $('#edit-vfo-mode').toggle(300);
   });

   $('span#vfo-a-width').click(function(e) {
      $('#edit-vfo-width').toggle(300);
   });

   $('span#vfo-a-power').click(function(e) {
      $('#edit-vfo-power').toggle(300);
   });

   // XXX: This should query the backend for available modes
   $.each(rig_modes, function(_, mode) {
      $('#rig-mode').append($('<option>').val(mode).text(mode));
   });

   $('#rig-mode').change(function(e) {
      // Send the change to the server
      var val = $(this).val();
      console.log("MODE changed to", val);;
      var msg = {
         msg: {
            type: "cat"
         },
         cat: {
            cmd: "mode",
            vfo: active_vfo,
            mode: val
         }
      };
      let json_msg = JSON.stringify(msg)
      socket.send(json_msg);
   });

   $('#rig-width').on('input', function() {
      $('#rig-width-val').text($(this).val());
   });
   $('#rig-power').on('input', function() {
      $('#rig-power-val').text($(this).val());
   });
   return false;
}

function ptt_btn_init() {
   ptt_button_apply();
   $('button.rig-ptt').click(function() {
      const vfo = active_vfo || 'A';
      const state = !Boolean(ptt_by_vfo[vfo]);
      ptt_pending = true;
      ptt_pending_state = state;
      ptt_button_apply();
      if (ptt_pending_timer) clearTimeout(ptt_pending_timer);
      ptt_pending_timer = setTimeout(function() {
         ptt_pending = false;
         ptt_button_apply();
      }, 2000);

      if (state) {
         if (typeof webui_start_microphone === "function") webui_start_microphone();
      } else {
         if (typeof webui_stop_microphone === "function") webui_stop_microphone();
      }

      var msg = {
         msg: {
            type: "cat"
         },
         cat: {
            cmd: "ptt",
            vfo: vfo,
            ptt: state ? "true" : "false"
         }
      };
      let json_msg = JSON.stringify(msg)
      socket.send(json_msg);
   });
}

function ptt_button_apply() {
   var other_tx = false;
   var tx_user = '';
   if (typeof UserCache !== 'undefined' && typeof UserCache.get_all === 'function') {
      UserCache.get_all().forEach(function(user) {
         if (parse_bool_field(user.ptt)) {
            other_tx = true;
            if (!tx_user) tx_user = user.name || '';
         }
      });
   }

   $('.rig-ptt').removeClass('red-btn green-btn yellow-btn tot-btn');
   ptt_set_vfo_locked(ptt_active);
   if (ptt_pending) {
      $('.rig-ptt').addClass('yellow-btn').html('PENDING');
   } else if (other_tx || ptt_active) {
      $('.rig-ptt').addClass('red-btn').html(tx_user ? 'TX: ' + tx_user : 'TX');
   } else {
      $('.rig-ptt').addClass('green-btn').html('PTT OFF');
   }
}

function ptt_set_vfo_locked(locked) {
   $('#rig-mode, #rig-width, #rig-power, #rig-apply').prop('disabled', locked);
   $('#edit-vfo-freq, #edit-vfo-mode, #edit-vfo-width, #edit-vfo-power, #rig-vfos')
      .toggleClass('ptt-locked', locked);
}

function ptt_user_is_local(user) {
   return !user || (typeof auth_user !== 'undefined' && auth_user &&
      String(user).toLowerCase() === String(auth_user).toLowerCase());
}

// Server talk-timeout (TOT) fired: orange button with TIMED OUT text until
// the next confirmed PTT state arrives. PARITY: rrclient/events.c
// rrclient_handle_ptt_tot() & rrclient/gtk.ptt-btn.c ptt_button_tot_expired()
function ptt_tot_expired(msgObj) {
   var tot = (msgObj["ptt.tot-expired"] && msgObj["ptt.tot-expired"].secs) ? msgObj["ptt.tot-expired"].secs : 300;
   var detail = msgObj["ptt.tot-expired"] || {};

   ptt_active = false;
   ptt_by_vfo[active_vfo || 'A'] = false;
   ptt_pending = false;
   if (ptt_pending_timer) clearTimeout(ptt_pending_timer);
   $('.rig-ptt').removeClass('red-btn green-btn yellow-btn tot-btn').addClass('tot-btn');
   $('.rig-ptt').html('TIMED OUT ' + tot + 's');

   if (typeof ChatBox !== 'undefined') {
      var who = detail.user || 'unknown user';
      var vfo = detail.vfo || '?';
      var freq = detail.freq || '?';
      var mode = detail.mode || '?';
      var width = detail.width || '?';
      ChatBox.Append('<div class="chat-status error">PTT Halted: ' + who +
         ' on VFO ' + vfo + ' @ ' + freq + ' Hz ' + mode + ' ' + width +
         ' Hz after ' + tot + ' seconds</div>');
   }
}

function webui_parse_cat_msg(msgObj) {
   var cat_ts = (msgObj.msg && msgObj.msg.ts) ? msgObj.msg.ts : msgObj.ts;
   var msg_ts = msg_timestamp(cat_ts);
   var cmd = msgObj.cat.cmd;
   var user = msgObj.cat.user;

//   console.log("Got CAT msg: ", msgObj);
   if (typeof msgObj.cat.cmd !== 'undefined') { // is it a command?
      var cmd = msgObj.cat.cmd.toLowerCase();
      if (cmd === 'ptt') {
         var vfo = msgObj.cat.vfo;
         var ptt = msgObj.cat.ptt;
         var ptt_vfo = msgObj.cat.vfo || active_vfo || 'A';
         var ptt_state = parse_bool_field(ptt);
         ptt_by_vfo[ptt_vfo] = ptt_state;
         if (ptt_user_is_local(user) && ptt_vfo === active_vfo) ptt_active = ptt_state;
         if (user) UserCache.update({ name: user, ptt: ptt_state });
         if (ptt_pending && ptt_pending_state === ptt_state && ptt_user_is_local(user)) {
            ptt_pending = false;
            if (ptt_pending_timer) clearTimeout(ptt_pending_timer);
         }
         ptt_button_apply();
      }
   } else if (cmd === 'freq') {  // broadcast of a user freq change
      var vfo = (msgObj.cat.vfo || 'A').toLowerCase();
      var freq = msgObj.cat.freq;
      if (typeof freq !== 'undefined' && freq > 0) {
         $('span#vfo-' + vfo + '-freq').html(format_freq(freq) + '&nbsp;Hz');
         freq_set_digits(freq, $('#rig-freq'));
         $('.vfo-changed').removeClass('vfo-changed');
      }
   } else if (cmd === 'mode') {  // broadcast of a user mode change
      var vfo = (msgObj.cat.vfo || 'A').toLowerCase();
      var mode = msgObj.cat.mode;
      if (typeof mode !== 'undefined') {
         $('span#vfo-' + vfo + '-mode').html(mode);
      }
   } else {  // Nope, it's a state message
      var state = msgObj.cat.state;
//      console.log("state:", state);

      if (typeof state === 'undefined') {
         return;
      }

      const { freq, mode, ptt, width, vfo, power }  = state;

      // The server is authoritative about which VFO is active (!vfo switches
      // it server-side and broadcasts cat.state.active with each cat.state).
      // PARITY: rrclient/vfo.c vfo_set_dict() (cat.state.active handling)
      if (state.active) {
         active_vfo = vfo;
         ptt_active = Boolean(ptt_by_vfo[active_vfo]);
         if (typeof mediaSyncActiveVfo === 'function') mediaSyncActiveVfo();
      }
      var vfo_id = (typeof vfo !== 'undefined' && vfo && vfo !== '-') ? vfo.toLowerCase() : 'a';

      if (typeof ptt !== 'undefined') {
         var ptt_state = parse_bool_field(ptt);
         var state_vfo = vfo || active_vfo || 'A';
         ptt_by_vfo[state_vfo] = ptt_state;
         if (ptt_user_is_local(user) && state_vfo === active_vfo) ptt_active = ptt_state;
         // cat.state carries the authoritative talker as `user`; keep the
         // cache in sync so a user's release clears TX:username immediately.
         if (user) UserCache.update({ name: user, ptt: ptt_state });
         if (ptt_pending && ptt_pending_state === ptt_state && ptt_user_is_local(user)) {
            ptt_pending = false;
            if (ptt_pending_timer) clearTimeout(ptt_pending_timer);
         }
         ptt_button_apply();
      }
      if (typeof freq !== 'undefined') {
         $('span#vfo-' + vfo_id + '-freq').html(format_freq(freq) + '&nbsp;Hz');
         let $input = $('#rig-freq');
         freq_set_digits(freq, $input);
         $('.vfo-changed').removeClass('vfo-changed');
      }

      if (typeof mode !== 'undefined') {
         $('span#vfo-' + vfo_id + '-mode').html(mode);
      }

      if (typeof width !== 'undefined') {
         $('span#vfo-' + vfo_id + '-width').html(width + '&nbsp;Hz');
      }

      if (typeof power !== 'undefined') {
         $('span#vfo-' + vfo_id + '-power').html(power + '&nbsp;W');
      }

      var ptt_user = '';
      if (typeof user !== 'undefined' && user !== '') {
         ptt_user = '<span>TX by ' + user + '</span>&nbsp';
      }

      var status_msg = '<span>VFO: ' + vfo + '</span>&nbsp' +
                       '<span>Mode:&nbsp;' +  mode + '&nbsp;</span>' +
                       '<span>Freq:' + format_freq(freq) + '</span>&nbsp;&nbsp;' +
                       '<span>Width:' + width + '</span>&nbsp;&nbsp;' +
                       ptt_user;
      // XXX: Power in the server msgs is actually rssid
     //                                '<span>RX: ' + power + '</span>';
      $('#chat-rig-status span#vfo-status').html(status_msg);
   }
}
