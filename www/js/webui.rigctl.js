/*
 * rig control (CAT over websocket)
 */
var active_vfo = 'A';		// Active VFO
var ptt_active = false;		// managed by webui.js for now when cat ptt comes
const FREQ_DIGITS = 10;		// How many digits of frequency to display - 10 digits = single ghz
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
         cat: {
            cmd: "mode",
            data: {
               vfo: active_vfo,
               mode: val
            }
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
   $('button.rig-ptt').removeClass('red-btn');
   $('button.rig-ptt').click(function() {
      let state = "off";

      // this is set via CAT messages
      if (ptt_active === false) {
         state = "on";
      } else {
         state = "off";
      }

      var msg = { 
         cat: {
            cmd: "ptt",
            data: {
               vfo: "A",
               state: state
            }
         }
      };
      let json_msg = JSON.stringify(msg)
      socket.send(json_msg);
   });
}

function webui_parse_cat_msg(msgObj) {
   var cat_ts = msgObj.ts;
   var msg_ts = msg_timestamp(cat_ts);
   var cmd = msgObj.cat.cmd;
   var user = msgObj.cat.user;

   if (typeof msgObj.cat.cmd !== 'undefined') { // is it a command?
      var cmd = msgObj.cat.cmd.toLowerCase();
      if (cmd === 'ptt') {
         var vfo = msgObj.cat.vfo;
         var ptt = msgObj.cat.state;
         var ptt_l = ptt.toLowerCase();

         if (ptt_l === "true" || ptt_l === "on" || ptt_l === 'yes' || ptt_l === true) {
            $('.rig-ptt').addClass("red-btn");
            ptt_active = true;
         } else {
            $('.rig-ptt').removeClass("red-btn");
            ptt_active = false;
         }
         UserCache.update({ name: user, ptt: ptt_active });
      }
    } else {  // Nope, it's a state message
      var state = msgObj.cat.state;
     //               console.log("state:", state);

      if (typeof state === 'undefined') {
         return;
      }

      const { freq, mode, ptt, width, vfo, power }  = state;
      if (typeof ptt !== 'undefined') {
         if (ptt === "false") {
            $('button.rig-ptt').removeClass("red-btn");
         } else {
            $('button.rig-ptt').addClass("red-btn");
         }
      }
      if (typeof freq !== 'undefined') {
         if (vfo === "A") {
            $('span#vfo-a-freq').html(format_freq(freq) + '&nbsp;Hz');
         } else if (vfo === "B") {
            $('span#vfo-b-freq').html(format_freq(freq) + '&nbsp;Hz');
         } else if (vfo === "C") {
            $('span#vfo-b-freq').html(format_freq(freq) + '&nbsp;Hz');
         }
         let $input = $('#rig-freq');
         freq_set_digits(freq, $input);
         $('.vfo-changed').removeClass('vfo-changed');
      }

      if (typeof mode !== 'undefined') {
         if (vfo === "A") {
            $('span#vfo-a-mode').html(mode);
         } else if (vfo === "B") {
            $('span#vfo-b-mode').html(mode);
         } else if (vfo === "C") {
            $('span#vfo-c-mode').html(mode);
         }
      }

      if (typeof width !== 'undefined') {
         if (vfo === "A") {
            $('span#vfo-a-width').html(width + '&nbsp;Hz');
         } else if (vfo === "B") {
            $('span#vfo-b-width').html(width + '&nbsp;Hz');
         } else if (vfo === "C") {
            $('span#vfo-c-width').html(width + '&nbsp;Hz');
         }
      }

      if (typeof power !== 'undefined') {
         if (vfo === "A") {
            $('span#vfo-a-power').html(power + '&nbsp;W');
         } else if (vfo === "B") {
            $('span#vfo-b-power').html(power + '&nbsp;W');
         } else if (vfo === "C") {
            $('span#vfo-c-power').html(power + '&nbsp;W');
         }
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
