/* Callsign alert matching regressions. SPDX-License-Identifier: GPL-2.0-only */
#include <rrclient/modem.callsign.h>
#include <assert.h>
int main(void) {
   assert(rr_modem_callsign_in_text("CQ K1ABC FN31","K1ABC"));
   assert(rr_modem_callsign_in_text("k1abc W1AW -12","K1ABC"));
   assert(!rr_modem_callsign_in_text("CQ DX K1ABC/P FN31","K1ABC"));
   assert(!rr_modem_callsign_in_text("CQ K1ABCD FN31","K1ABC"));
   assert(!rr_modem_callsign_in_text("CQ /K1ABC FN31","K1ABC"));
   assert(!rr_modem_callsign_in_text("K1ABC/7 W1AW","K1ABC"));
   assert(!rr_modem_callsign_in_text("CQ W1AW",""));
   assert(!rr_modem_callsign_in_text(NULL,"K1ABC"));
   assert(!rr_modem_callsign_in_text("CQ W1AW",NULL));
   return 0;
}
