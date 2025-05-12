if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_syslog_init() {
  // do syslog-related setup here
});

function syslog_clear() {
   $('#syslog').empty();
}

function syslog_append(msgObj) {
   const el = document.getElementById('win-syslog');
   const wasAtBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 1;

   var message = msgObj.syslog.data;
   var msg_ts = msgObj.syslog.ts;
   var msg_prio = msgObj.syslog.prio;
   var msg_subsys = msgObj.syslog.subsys;
   var human_ts = msg_timestamp(msg_ts);

   var msg = `<div class="syslog-msg">${human_ts}&nbsp&lt;${msg_subsys}/${msg_prio}&gt;&nbsp;${message}</div>`;
   $('#syslog').append(msg);

   if (wasAtBottom) {
      requestAnimationFrame(() => {
         el.scrollTop = el.scrollHeight;
      });
   }
}

function show_syslog_window() {
//   $('.chroma-hash').hide();
   $('div#win-rig').hide();
   $('div#win-chat').hide();
   $('div#win-login').hide();
   $('div#win-config').hide();
   $('div#tabstrip').show();
   $('div#win-syslog').show();
   const el = document.getElementById('win-syslog');

   requestAnimationFrame(() => {
      el.scrollTop = el.scrollHeight;
   });

}
