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
