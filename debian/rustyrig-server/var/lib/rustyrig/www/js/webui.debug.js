
// PARITY: rrclient/ui.gtk3/gtk.editcfg.c uses the same "reload stylesheets only" semantics
// Support reloading the stylesheet (/reloadcss) without restarting the app
function reload_css() {
   const sheets = document.querySelectorAll('link[rel="stylesheet"]');

   if (!sheets.length) {
      console.log("reload_css: No stylesheets found, nothing to do");
      return;
   }

   sheets.forEach(function(link) {
      const href = (link.getAttribute('href') || '').split('?')[0];

      if (!href) {
         return;
      }

      link.setAttribute('href', href + '?_=' + Date.now());
   });

   setTimeout(function() {
      const chatBox = document.getElementById('chat-box');

      if (chatBox) {
         chatBox.scrollTop = chatBox.scrollHeight;
      }
   }, 250);
}
