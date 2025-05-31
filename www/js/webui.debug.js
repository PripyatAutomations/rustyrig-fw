
// Support reloading the stylesheet (/reloadcss) without restarting the app
function reload_css() {
  $('link[rel="stylesheet"]').each(function() {
    let $link = $(this);
    let href = $link.attr('href').split('?')[0];
    $link.attr('href', href + '?_=' + new Date().getTime());
  });

   setTimeout(function() {
      let chatBox = $('#chat-box');
      chatBox.scrollTop(chatBox[0].scrollHeight);
   }, 250);
}
