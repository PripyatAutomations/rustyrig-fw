///////////////
// Windowing //
///////////////
var available_tabs = [ 'login', 'rig', 'chat', 'syslog', 'cfg' ];
var active_tab = 'chat';

function wmSwitchTab(tab) {
   // Make sure it's a valid tab name
   if (available_tabs.includes(tab)) {
      console.log('[wm] wmSwitchTab =>', tab);
      // Iterate
      available_tabs.forEach((i) => {
         var div_name = 'div#win-' + i;

         // If this isn't the desired tab, hide it (so we don't flicker the current tab)
         if (tab !== i) {
            console.log('[wm] hiding', i);
            $(div_name).hide('slow');
         }
      });

      // Ensure the tab strip isn't show at login screen
      if (tab !== 'login') {
         $('div#tabstrip').show('slow');
      } else {
         $('div#tabstrip').hide('slow');

         // Re-enable login button
         $('button#login-submit-btn').prop('disabled', false);
         $('form#login input#user').focus();
      }

      // And finally, show the desired tab
      $('div#win-' + tab).show('slow');
      active_tab = tab;

      // indicate success
      return false;
   } else {
      console.log('[wm] wmSwitchTab called for invalid tab', tab, ' valid choices:', available_tabs);
      return true;
   }
}

const el = document.getElementById('win-syslog');

requestAnimationFrame(() => {
   el.scrollTop = el.scrollHeight;
});
