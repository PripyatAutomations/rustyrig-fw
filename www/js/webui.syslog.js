function syslog_clear() {
   $('#syslog').empty();
}

function syslog_append(msg) {
   $('#syslog').append(msg);
   setTimeout(function () {
      $('#syslog').scrollTop($('#syslog')[0].scrollHeight);
   }, 10);
}
