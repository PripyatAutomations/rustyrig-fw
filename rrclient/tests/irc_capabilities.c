#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/irc.h>
time_t now = 100;
bool dying, restarting;
static unsigned changes;
static long long clock_ms;
long long mono_ms(void) {
   return clock_ms;
}
static void changed(const char *event, const char *data, rrconn_t *c, void *user) {
   (void)event;
   (void)data;
   (void)c;
   (void)user;
   changes++;
}
static void wire(struct mg_connection *c, const char *expected) {
   assert(c->send.len == strlen(expected));
   assert(!memcmp(c->send.buf, expected, c->send.len));
   mg_iobuf_del(&c->send, 0, c->send.len);
}
int main(void) {
   rrconn_t a = {
      .fd = -1, .connected = now
   }, b = {
      .fd = -1
   };
   struct mg_connection c = {
      0
   };
   a.conn = &c;
   event_init();
   assert(!irc_init());
   event_on("irc.capabilities", changed, NULL);
   assert(irc_line_limit(&a) == 512 && irc_line_limit(&b) == 512);
   irc_process_message(&a, ":server 005 nick LINELEN=768 CASEMAPPING=rfc1459 EXCEPTS CUSTOM=a=b EMPTY= :supported");
   assert(irc_line_limit(&a) == 768 && irc_line_limit(&b) == 512);
   assert(!strcmp(irc_server_property(&a, "casemapping"), "rfc1459"));
   assert(!strcmp(irc_server_property(&a, "CUSTOM"), "a=b"));
   assert(!strcmp(irc_server_property(&a, "EXCEPTS"), ""));
   assert(!strcmp(irc_server_property(&a, "EMPTY"), ""));
   assert(!irc_server_property(&a, "supported"));
   irc_process_message(&a, ":server 005 nick -CUSTOM LINELEN=80 :supported");
   assert(!irc_server_property(&a, "CUSTOM") && irc_line_limit(&a) == 80);
   irc_process_message(&a, ":server 005 nick LINELEN=99999999999999999999999999 :supported");
   assert(irc_line_limit(&a) == IRC_MSGLEN);
   irc_process_message(&a, ":server 005 nick LINELEN=12bad :supported");
   assert(irc_line_limit(&a) == 512);
   irc_capabilities_reset(&a);
   assert(!irc_server_property(&a, "CASEMAPPING"));
   assert(!irc_supports_rustyrig(&a));
   irc_process_message(&a, ":server CAP * LS * :multi-prefix RUSTYRIG-other custom=value sasl=PLAIN,EXTERNAL");
   assert(!strcmp(irc_server_capability(&a, "sasl"), "PLAIN,EXTERNAL"));
   assert(!irc_capability_enabled(&a, "sasl") && !irc_server_capability(&b, "sasl"));
   assert(!c.send.len && !irc_supports_rustyrig(&a));
   irc_process_message(&a, ":server CAP * LS :RUSTYRIG sasl");
   wire(&c, "CAP REQ :multi-prefix RUSTYRIG\r\n");
   assert(!irc_supports_rustyrig(&a));
   assert(!strcmp(irc_server_capability(&a, "custom"), "value"));
   irc_process_message(&a, ":server CAP * ACK :multi-prefix RUSTYRIG");
   wire(&c, "CAP END\r\n");
   assert(irc_supports_rustyrig(&a) && !irc_supports_rustyrig(&b) && changes == 1);
   assert(irc_capability_enabled(&a, "multi-prefix"));
   irc_process_message(&a, ":server CAP * NEW :custom=updated new-cap");
   assert(!strcmp(irc_server_capability(&a, "custom"), "updated"));
   irc_process_message(&a, ":server CAP * ACK :custom");
   assert(irc_capability_enabled(&a, "custom"));
   irc_process_message(&a, ":server CAP * ACK :-custom");
   assert(!irc_capability_enabled(&a, "custom"));
   irc_process_message(&a, ":server CAP * DEL :custom RUSTYRIG");
   assert(!irc_server_capability(&a, "custom") && !irc_capability_enabled(&a, "RUSTYRIG"));
   irc_process_message(&a, "CAPAB :QS EX CUSTOM=value");
   assert(!strcmp(irc_server_capab(&a, "QS"), ""));
   assert(!strcmp(irc_server_capab(&a, "CUSTOM"), "value"));
   irc_process_message(&a, "CAPAB :-QS CUSTOM=updated");
   assert(!irc_server_capab(&a, "QS") && !strcmp(irc_server_capab(&a, "CUSTOM"), "updated"));
   assert(!irc_supports_rustyrig(&a) && changes == 2);
   irc_process_message(&a, ":server 005 nick RUSTYRIG PREFIX=(ov)@+ CHANMODES=beI,k,l,imnpst :supported");
   assert(irc_supports_rustyrig(&a));
   assert(irc_prefix_mode(&a, '@') == 'o' && irc_prefix_mode(&a, '+') == 'v' && !irc_prefix_mode(&a, '~'));
   assert(!strcmp(irc_modes_symbol(&a, "vo"), "@"));
   assert(!strcmp(irc_modes_symbol(&a, "v"), "+"));
   assert(irc_mode_has_argument(&a, 'b', false) && irc_mode_has_argument(&a, 'k', false));
   assert(irc_mode_has_argument(&a, 'l', true) && !irc_mode_has_argument(&a, 'l', false));
   assert(!irc_mode_has_argument(&a, 'i', true));
   irc_process_message(&a, ":server 005 nick -RUSTYRIG PREFIX=(ov)!? :supported");
   assert(!irc_supports_rustyrig(&a) && !strcmp(irc_modes_symbol(&a, "o"), "!"));
   irc_capabilities_reset(&a);
   irc_process_message(&a, ":server CAP * LS :sasl echo-message");
   wire(&c, "CAP END\r\n");
   irc_capabilities_reset(&a);
   irc_process_message(&a, ":server CAP * LS :RUSTYRIG");
   wire(&c, "CAP REQ :RUSTYRIG\r\n");
   irc_process_message(&a, ":server CAP * NAK :RUSTYRIG");
   wire(&c, "CAP END\r\n");
   assert(!irc_supports_rustyrig(&a));
   irc_process_message(&a, ":server CAP * NEW :example=value other");
   irc_process_message(&a, ":server CAP * ACK :example other");
   assert(irc_capability_enabled(&a, "example") && irc_capability_enabled(&a, "other"));
   irc_process_message(&a, ":server CAP * LIST * :example");
   assert(irc_capability_enabled(&a, "example") && !irc_capability_enabled(&a, "other"));
   irc_process_message(&a, ":server CAP * LIST :other");
   assert(irc_capability_enabled(&a, "example") && irc_capability_enabled(&a, "other"));
   irc_process_message(&a, ":server CAP * NAK :other");
   assert(!irc_capability_enabled(&a, "other") && irc_server_capability(&a, "other"));
   irc_process_message(&a, ":server CAP * LS :new-snapshot");
   assert(!irc_server_capability(&a, "example") && irc_server_capability(&a, "new-snapshot"));
   irc_capabilities_clear(&a);
   assert(!irc_server_capability(&a, "sasl") && !irc_server_capab(&a, "CUSTOM"));
   assert(!irc_server_property(&a, "PREFIX") && irc_line_limit(&a) == 512);
   server_cfg_t sasl = {
      .sasl_user = "account", .sasl_pass = "password", .sasl_mechanism = "PLAIN"
   };
   a.server = &sasl;
   irc_capabilities_reset(&a);
   irc_process_message(&a, ":server CAP * LS :multi-prefix sasl=PLAIN,EXTERNAL");
   wire(&c, "CAP REQ :multi-prefix sasl\r\n");
   irc_process_message(&a, ":server CAP * ACK :multi-prefix");
   assert(!c.send.len);         // Split ACK must not end CAP before SASL starts.
   irc_process_message(&a, ":server CAP * ACK :sasl");
   wire(&c, "AUTHENTICATE PLAIN\r\n");
   irc_process_message(&a, "AUTHENTICATE +");
   wire(&c, "AUTHENTICATE AGFjY291bnQAcGFzc3dvcmQ=\r\n");
   irc_process_message(&a, ":server 903 nick :SASL successful");
   wire(&c, "CAP END\r\n");
   irc_capabilities_reset(&a);
   irc_process_message(&a, ":server CAP * LS :sasl");
   wire(&c, "CAP REQ :sasl\r\n");
   irc_process_message(&a, ":server CAP * ACK :sasl");
   wire(&c, "AUTHENTICATE PLAIN\r\n");
   irc_process_message(&a, ":server 904 nick :SASL failed");
   wire(&c, "CAP END\r\n");
   strcpy(sasl.sasl_mechanism, "EXTERNAL");
   irc_capabilities_reset(&a);
   irc_process_message(&a, ":server CAP * LS :sasl=EXTERNAL");
   wire(&c, "CAP REQ :sasl\r\n");
   irc_process_message(&a, ":server CAP * ACK :sasl");
   wire(&c, "AUTHENTICATE EXTERNAL\r\n");
   irc_process_message(&a, "AUTHENTICATE +");
   wire(&c, "AUTHENTICATE +\r\n");
   irc_process_message(&a, ":server 903 nick :SASL successful");
   wire(&c, "CAP END\r\n");
   // Exact 400-byte responses require the final '+' delimiter.
   strcpy(sasl.sasl_mechanism, "PLAIN");
   memset(sasl.sasl_user, 'a', 170);
   sasl.sasl_user[170] = 0;
   memset(sasl.sasl_pass, 'b', 128);
   sasl.sasl_pass[128] = 0;
   irc_capabilities_reset(&a);
   irc_process_message(&a, ":server CAP * LS :sasl");
   wire(&c, "CAP REQ :sasl\r\n");
   irc_process_message(&a, ":server CAP * ACK :sasl");
   wire(&c, "AUTHENTICATE PLAIN\r\n");
   irc_process_message(&a, "AUTHENTICATE +");
   assert(c.send.len == 431 && !memcmp(c.send.buf, "AUTHENTICATE ", 13));
   assert(!memcmp(c.send.buf + 413, "\r\nAUTHENTICATE +\r\n", 18));
   mg_iobuf_del(&c.send, 0, c.send.len);
   irc_process_message(&a, ":server 906 nick :aborted");
   wire(&c, "CAP END\r\n");
   irc_capabilities_reset(&a);
   irc_process_message(&a, ":server CAP * LS :sasl");
   wire(&c, "CAP REQ :sasl\r\n");
   irc_process_message(&a, ":server CAP * ACK :sasl");
   wire(&c, "AUTHENTICATE PLAIN\r\n");
   clock_ms = 30001;
   irc_capabilities_poll(&a);
   wire(&c, "AUTHENTICATE *\r\nCAP END\r\n");
   irc_capabilities_poll(&a);
   assert(!c.send.len);
   puts("PASS: IRC SASL PLAIN/EXTERNAL, split ACK, success/failure and 400-byte framing");
   irc_shutdown();
   event_shutdown();
   mg_iobuf_free(&c.send);
   puts("PASS: isolated IRC capabilities, multiline CAP negotiation, rejection, removal, PREFIX and mode arguments");
}
