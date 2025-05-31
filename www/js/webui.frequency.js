
function freq_update_digit($digit, delta) {
   let val = parseInt($digit.find('.value').text(), 10);
   var newVal;

   if (delta != 0) {
      newVal = val + delta;
   } else {
      newVal = 0;
   }

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

      $container.on('dblclick', '.digit .value', function() {
         let $digit = $(this).closest('.digit');

         freq_update_digit($digit, 0);
         let onChange = $container.data('onChange');
         if (onChange) {
            onChange();
         }
      });
   }

   let $input = $('#rig-freq');
   freq_init_digits($input, function(val) {
      // Blorp out the change as a command
      var msg = { 
         cat: {
            cmd: "freq",
            data: {
               vfo: active_vfo,
               freq: val
            }
         }
      };
      let json_msg = JSON.stringify(msg)
      socket.send(json_msg);
//      console.log("setting vfo", active_vfo, "freq", val);
      $input.addClass('vfo-changed');
   });
}

function format_freq(freq) {
   let mhz = (freq / 1000).toFixed(3);               // to decimal khz
   return mhz.replace(/\B(?=(\d{3})+(?!\d))/g, ","); // Add comma
}
