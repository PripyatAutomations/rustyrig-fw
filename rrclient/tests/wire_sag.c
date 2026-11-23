// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <rrclient/gtk/gtk.wire-sag.h>

static int near(double a,double b) { return fabs(a-b)<1.e-7; }
int main(void) {
   double x,y,cx,cy0,cy1;
   rr_patchbay_wire_controls(10,20,110,20,0,&cx,&cy0,&cy1);
   assert(near(cx,60) && near(cy0,20) && near(cy1,20));
   rr_patchbay_wire_point(10,20,110,20,0,0.5,&x,&y);
   assert(near(x,60) && near(y,20));
   rr_patchbay_wire_point(10,20,110,20,100,0.5,&x,&y);
   assert(near(x,60) && near(y,95)); /* midpoint droops by 75% sag */
   rr_patchbay_wire_point(10,20,110,20,100,0,&x,&y);
   assert(near(x,10) && near(y,20));
   rr_patchbay_wire_point(10,20,110,20,100,1,&x,&y);
   assert(near(x,110) && near(y,20));
   rr_patchbay_wire_point(10,20,110,80,100,.5,&x,&y);
   assert(near(x,60) && near(y,125)); /* endpoints stay on pins */
   rr_patchbay_wire_point(10,20,110,20,-40,.5,&x,&y);
   assert(near(y,20));
   rr_patchbay_wire_point(10,20,110,20,400,.5,&x,&y);
   assert(near(y,200)); /* clamp at 240 */
   rr_patchbay_wire_point(10,20,110,20,NAN,.5,&x,&y);
   assert(isfinite(x) && isfinite(y) && near(y,20));
   puts("PASS: patchbay sag geometry, pinned endpoints, saturation and invalid values");
   return 0;
}
