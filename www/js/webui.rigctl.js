/*
 * rig control (CAT over websocket)
 */
var ptt_active = false;
var freq = 7074000;
var freq_b = 14074000;

if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_rigctl_init() {
   let status = "OK";

   console.log("rigctl init: start");
   console.log("rigctl init: end. status=" + status);


   $('button#rig-ptt').click(function() {
      console.log("ptt: ", ptt_active);
      let state = "off";
      if (ptt_active === false) {
         state = "on";
         ptt_active = true;
      } else {
         state = "off";
         ptt_active = false;
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
      console.log("====> ", json_msg);
      socket.send(json_msg);
   });

   $('span#vfo-a-freq, span#vfo-b-freq').html(freq);
   $('span#vfo-b-freq').html(freq_b);

   // XXX: we should make this take a container div as argument
   freq_input_init();
});


function freq_input_init() {
   const DIGITS = 10;

   function freq_create_digit(index) {
      return $(`
         <div class="digit" data-index="${index}">
            <button class="inc">+</button>
            <div class="value">0</div>
            <button class="dec">-</button>
         </div>
      `);
   }

   function freq_update_digit($digit, delta) {
      let val = parseInt($digit.find('.value').text(), 10);
      let newVal = val + delta;
      let $container = $digit.closest('.digit-container');

      if (newVal > 9) {
         $digit.find('.value').text('0');
         let idx = parseInt($digit.attr('data-index'));
         if (idx > 0)
            freq_update_digit($container.find('.digit').eq(idx - 1), +1);
      } else if (newVal < 0) {
         $digit.find('.value').text('9');
         let idx = parseInt($digit.attr('data-index'));
         if (idx > 0)
            freq_update_digit($container.find('.digit').eq(idx - 1), -1);
      } else {
         $digit.find('.value').text(newVal);
      }

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
      let str = String(val).padStart(DIGITS, '0');
      let $digits = $container.find('.digit');
      for (let i = 0; i < DIGITS; i++) {
         $digits.eq(i).find('.value').text(str[i]);
      }
   }

   function freq_init_digits($container, onChange = null) {
      $container.empty();
      for (let i = 0; i < DIGITS; i++) {
         $container.append(freq_create_digit(i));
      }

      if (onChange)
         $container.data('onChange', onChange);

      $container.on('click', '.inc', function() {
         let $digit = $(this).closest('.digit');
         freq_update_digit($digit, +1);
      });

      $container.on('click', '.dec', function() {
         let $digit = $(this).closest('.digit');
         freq_update_digit($digit, -1);
      });
   }

   $('span#vfo-a-freq, span#vfo-b-freq').html(freq);
   $('span#vfo-b-freq').html(freq_b);

   let $input = $('#rig-freq');
   freq_init_digits($input, function(val) {
       $('span#vfo-a-freq').html(val);
       console.log("New freq: ", val);
   });
   freq_set_digits(freq, $input);
}
