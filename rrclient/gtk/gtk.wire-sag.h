// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_GTK_WIRE_SAG_H
#define RRCLIENT_GTK_WIRE_SAG_H
#include <math.h>

/* Canvas units, independent of zoom. Sag 0 preserves the original
 * horizontal-tangent cubic; positive values lower both control points.
 * One curve definition is used for drawing and pointer hit-testing. */
static inline void rr_patchbay_wire_controls(double x0,double y0,
   double x1,double y1,double sag,double *cx,double *cy0,double *cy1)
{
   if (!isfinite(sag) || sag < 0.) sag=0.;
   if (sag > 240.) sag=240.;
   *cx=(x0+x1)*.5;
   *cy0=y0+sag;
   *cy1=y1+sag;
}
static inline void rr_patchbay_wire_point(double x0,double y0,
   double x1,double y1,double sag,double t,double *x,double *y)
{
   double cx,cy0,cy1;
   rr_patchbay_wire_controls(x0,y0,x1,y1,sag,&cx,&cy0,&cy1);
   double u=1.-t;
   *x=u*u*u*x0+3*u*u*t*cx+3*u*t*t*cx+t*t*t*x1;
   *y=u*u*u*y0+3*u*u*t*cy0+3*u*t*t*cy1+t*t*t*y1;
}
#endif
