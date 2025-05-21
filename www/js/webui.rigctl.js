/*
 * rig control (CAT over websocket)
 */
var ptt_active = false;		// managed by webui.js for now when cat ptt comes
const FREQ_DIGITS = 10;

if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_rigctl_init() {
   let status = "OK";

   console.log("rigctl init: start");
   console.log("rigctl init: end. status=" + status);

   vfo_edit_init();
   ptt_btn_init();
   freq_input_init();
});

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

   let modes = [ 'LSB', 'USB', 'AM', 'FM', 'D-L', 'D-U' ];

   $.each(modes, function(_, mode) {
      $('#rig-mode').append($('<option>').val(mode).text(mode));
   });

   /* XXX: fix the naming here */
   $('#tx-bw').on('input', function() {
      $('#tx-bw-val').text($(this).val());
   });
   $('#tx-pow').on('input', function() {
      $('#tx-pow-val').text($(this).val());
   });
}

function ptt_btn_init() {
   $('button#rig-ptt').click(function() {
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

function freq_update_digit($digit, delta) {
   let val = parseInt($digit.find('.value').text(), 10);
   let newVal = val + delta;
   let $container = $digit.closest('.digit-container');

   if (newVal > 9) {
      $digit.find('.value').text('0');
      let idx = parseInt($digit.attr('data-index'));

      if (idx > 0) {
         freq_update_digit($container.find('.digit').eq(idx - 1), +1);
      }
   } else if (newVal < 0) {
      $digit.find('.value').text('9');
      let idx = parseInt($digit.attr('data-index'));

      if (idx > 0) {
         freq_update_digit($container.find('.digit').eq(idx - 1), -1);
      }
   } else {
      $digit.find('.value').text(newVal);
   }

   $digit.find('.value').addClass('vfo-changed');

   let onChange = $container.data('onChange');
   if (typeof onChange === 'function') {
      onChange(freq_get_digits($container));
   }
}

function freq_get_digits($container = $('#custom-input')) {
   let raw = $container.find('.digit .value').map(function() {
      return $(this).text();
   }).get().join('');
   return String(parseInt(raw, 10));  // Removes leading zeros
}

function freq_set_digits(val, $container = $('#custom-input')) {
   let str = String(val).padStart(FREQ_DIGITS, '0');
   let $digits = $container.find('.digit');

   for (let i = 0; i < FREQ_DIGITS; i++) {
      $digits.eq(i).find('.value').text(str[i]);
   }
}

function freq_create_digit(index) {
   return $(`
      <div class="digit" data-index="${index}">
         <button class="inc">+</button>
         <div class="value">0</div>
         <button class="dec">-</button>
      </div>
   `);
}

function freq_input_init() {
   function freq_init_digits($container, onChange = null) {
      $container.empty();

      for (let i = 0; i < FREQ_DIGITS; i++) {
         $container.append(freq_create_digit(i));

         let remaining = FREQ_DIGITS - i - 1;
         if (remaining > 0 && remaining % 3 === 0) {
            $container.append('<div class="digit-spacer"></div>');
         }
      }

      if (onChange) {
         $container.data('onChange', onChange);
      }

      $container.on('click', '.inc', function() {
         let $digit = $(this).closest('.digit');
         freq_update_digit($digit, +1);
      });

      $container.on('click', '.dec', function() {
         let $digit = $(this).closest('.digit');
         freq_update_digit($digit, -1);
      });

      /* XXX: fix this to support sending the changed result */
/*
      $container.on('dblclick', '.digit .value', function() {
         $(this).text('0');

         let onChange = $container.data('onChange');
         if (onChange) {
            onChange();
         }
      });
*/
   }

   let $input = $('#rig-freq');
   freq_init_digits($input, function(val) {
      // Blorp out the change as a command
      var msg = { 
         cat: {
            cmd: "freq",
            data: {
               vfo: "A",
               freq: val
            }
         }
      };
      let json_msg = JSON.stringify(msg)
      socket.send(json_msg);
      console.log("setting vfo A", active_vfo, "freq", val);
      $input.addClass('vfo-changed');
   });
}

function format_freq(freq) {
   let mhz = (freq / 1000).toFixed(3); // 72000000 → "72000.000"
   return mhz.replace(/\B(?=(\d{3})+(?!\d))/g, ","); // Add comma
}
