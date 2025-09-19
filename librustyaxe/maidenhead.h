#if	!defined(_maidenhead_h)
#define	_maidenhead_h
#include <math.h>
#if	!defined(pi)
#define pi 3.14159265358979323846
#endif


#ifdef __cplusplus
extern "C" {
#endif
   typedef struct Coordinates {
      float	latitude;
      float	longitude;
      uint8_t   precision;                      // how many digits of precision were given for the longest of lat/lon
      int	error;
   } Coordinates;


   // Only FT4 and FT8 are supported by ft8_lib, but we can talk to ardop
   typedef enum {
     TX_MODE_NONE = 0,
     TX_MODE_FT8,
     TX_MODE_FT4,
//     TX_MODE_JS8,
//     TX_MODE_PSK11,
//     TX_MODE_ARDOP_FEC,
     TX_MODE_END		// invalid, end of list marker
   } tx_mode_t;

   extern double deg2rad(double deg);
   extern double rad2deg(double rad);
   extern Coordinates maidenhead2latlon(const char *locator);
   extern const char *latlon2maidenhead(Coordinates *c);
   extern double calculateBearing(double lat1, double lon1, double lat2, double lon2);
   extern double calculateDistance(double lat1, double lon1, double lat2, double lon2);

#ifdef __cplusplus
};
#endif

#endif	// !defined(_maidenhead_h)
